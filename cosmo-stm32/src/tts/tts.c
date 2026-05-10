/**
 * @file    tts.c
 * @brief   Text-to-Speech subsystem implementation (pre-recorded clips, MVP).
 *
 * Layered design (matches tts.h public surface):
 *   Layer 1 — storage access (Flash today, SD card later)
 *   Layer 2 — response lookup (linear scan of response_database)
 *   Layer 3 — audio processing (validation, peak check)
 *   Layer 4 — state machine bookkeeping
 *   Layer 5 — public API consumed by the FSM
 *
 * No dynamic allocation, no blocking I/O. All audio data is referenced by
 * pointer into Flash; the audio playback subsystem reads it directly.
 */

#include "tts.h"
#include "response_database.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* ────────────────────────── Platform glue ──────────────────────────────── *
 * system_get_tick_ms() / tts_log() are normally provided by the BSP. Provide
 * weak fallbacks so this file builds standalone (host unit tests, IDE
 * intellisense). The real implementations override these at link time.
 */

#ifndef TTS_HAS_BSP
__attribute__((weak)) uint32_t system_get_tick_ms(void) { return 0U; }
__attribute__((weak)) void     tts_log(const char *msg) { (void)msg;   }
#else
extern uint32_t system_get_tick_ms(void);
extern void     tts_log(const char *msg);
#endif

/* ────────────────────────── Global state ───────────────────────────────── */

tts_context_t tts;

/* Small scratch buffer for log formatting; kept .bss-resident so we never
 * blow the stack from inside an FSM handler.                                */
static char tts_log_scratch[160];

/* ────────────────────────── Internal helpers ───────────────────────────── */

static void tts_logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(tts_log_scratch, sizeof(tts_log_scratch), fmt, ap);
    va_end(ap);
    tts_log(tts_log_scratch);
}

/** Compute peak absolute sample value across the buffer. */
static int32_t tts_compute_peak_level(const int16_t *audio, uint32_t n)
{
    int32_t peak = 0;
    for (uint32_t i = 0U; i < n; ++i) {
        int32_t v = audio[i] < 0 ? -(int32_t)audio[i] : (int32_t)audio[i];
        if (v > peak) {
            peak = v;
        }
    }
    return peak;
}

/* ────────────────────────── Layer 1: Storage access ────────────────────── */

const int16_t *tts_load_audio_from_flash(const int16_t *audio_array,
                                         uint32_t       known_size,
                                         uint32_t      *size_out)
{
    if (audio_array == NULL || size_out == NULL) {
        return NULL;
    }
    *size_out = known_size;
    return audio_array;
}

/* SD-card path is reserved for a future enhancement; stub returns NULL so
 * any accidental call surfaces immediately as a load failure rather than a
 * silent crash.                                                             */
const int16_t *tts_load_audio_from_sd(const char *filename,
                                      uint32_t   *size_out)
{
    (void)filename;
    if (size_out != NULL) {
        *size_out = 0U;
    }
    return NULL;
}

/* ────────────────────────── Layer 2: Response lookup ───────────────────── */

const response_clip_t *tts_find_response(const char *response_text)
{
    if (response_text == NULL) {
        return NULL;
    }

    /* Exact match — the dialogue engine emits text drawn from the same
     * source-of-truth strings that populate the database, so equality is
     * the common case.                                                       */
    for (uint32_t i = 0U; i < response_database_size; ++i) {
        const char *db_text = response_database[i].response_text;
        if (db_text != NULL && strcmp(db_text, response_text) == 0) {
            return &response_database[i];
        }
    }

    /* Fuzzy fallback: substring containment in either direction. Cheap, and
     * tolerates trailing punctuation or minor paraphrasing without pulling
     * in a full Levenshtein implementation.                                  */
    for (uint32_t i = 0U; i < response_database_size; ++i) {
        const char *db_text = response_database[i].response_text;
        if (db_text == NULL) continue;
        if (strstr(db_text, response_text) != NULL ||
            strstr(response_text, db_text) != NULL) {
            return &response_database[i];
        }
    }

    return NULL;
}

uint8_t tts_select_response_variant(const char            *intent_prefix,
                                    const response_clip_t **out_clip)
{
    if (intent_prefix == NULL || out_clip == NULL) {
        return 1U;
    }

    /* Two-pass: first count matches by source filename prefix, then pick
     * the Nth one. Avoids allocating an index array.                         */
    const size_t plen = strlen(intent_prefix);
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < response_database_size; ++i) {
        const char *fn = response_database[i].source_filename;
        if (fn != NULL && strncmp(fn, intent_prefix, plen) == 0) {
            ++count;
        }
    }
    if (count == 0U) {
        *out_clip = NULL;
        return 1U;
    }

    uint32_t pick = (uint32_t)rand() % count;
    uint32_t seen = 0U;
    for (uint32_t i = 0U; i < response_database_size; ++i) {
        const char *fn = response_database[i].source_filename;
        if (fn != NULL && strncmp(fn, intent_prefix, plen) == 0) {
            if (seen == pick) {
                *out_clip = &response_database[i];
                return 0U;
            }
            ++seen;
        }
    }

    *out_clip = NULL;
    return 1U;
}

/* ────────────────────────── Layer 3: Audio processing ──────────────────── */

void tts_prepare_audio_for_playback(const int16_t *audio_data,
                                    uint32_t       sample_count)
{
    if (audio_data == NULL || sample_count == 0U) {
        tts_set_error(TTS_ERROR_INVALID_INPUT,
                      "prepare: null buffer or zero length");
        return;
    }

    /* Peak check is advisory — we still publish the buffer either way, we
     * just leave a breadcrumb in the log for the audio-tuning pass.         */
    int32_t peak = tts_compute_peak_level(audio_data, sample_count);
    if (peak < TTS_PEAK_QUIET_THRESHOLD) {
        tts_logf("[tts] warn: clip is quiet (peak=%ld)", (long)peak);
    } else if (peak > TTS_PEAK_LOUD_THRESHOLD) {
        tts_logf("[tts] warn: clip near clipping (peak=%ld)", (long)peak);
    }

    tts.output_buffer       = audio_data;
    tts.output_buffer_size  = sample_count;
    tts.output_duration_ms  = (uint16_t)((sample_count * 1000U)
                                          / TTS_AUDIO_SAMPLE_RATE_HZ);
}

/* ────────────────────────── Layer 4: State management ──────────────────── */

void tts_set_state(tts_state_t new_state)
{
    static const char *names[] = {
        "IDLE", "SEARCHING", "FOUND", "ERROR"
    };
    if ((unsigned)new_state < (sizeof(names) / sizeof(names[0]))) {
        tts_logf("[tts] state -> %s", names[new_state]);
    }
    tts.state = new_state;
}

void tts_set_error(uint8_t error_code, const char *error_message)
{
    tts.state      = TTS_STATE_ERROR;
    tts.error_code = error_code;

    if (error_message != NULL) {
        (void)strncpy(tts.error_message, error_message,
                      TTS_ERROR_MESSAGE_MAX - 1U);
        tts.error_message[TTS_ERROR_MESSAGE_MAX - 1U] = '\0';
    } else {
        tts.error_message[0] = '\0';
    }

    tts_logf("[tts] error %u: %s",
             (unsigned)error_code, tts.error_message);
}

/* ────────────────────────── Layer 5: Public API ────────────────────────── */

void tts_init(void)
{
    (void)memset(&tts, 0, sizeof(tts));
    tts.state              = TTS_STATE_IDLE;
    tts.error_code         = TTS_ERROR_NONE;
    tts.output_buffer      = NULL;
    tts.output_buffer_size = 0U;
    tts.output_duration_ms = 0U;

    /* Seed RNG used by tts_select_response_variant. system_get_tick_ms()
     * is fine as a seed source for variant rotation — we don't need it to
     * be cryptographic.                                                     */
    srand((unsigned int)system_get_tick_ms());

    tts_log("[tts] init complete");
}

void tts_process(const char *response_text)
{
    tts.processing_start_time = system_get_tick_ms();

    /* 1. Validate input. */
    if (response_text == NULL || response_text[0] == '\0') {
        tts_set_error(TTS_ERROR_INVALID_INPUT,
                      "tts_process: null or empty text");
        return;
    }

    /* 2. Move into SEARCHING. */
    tts_set_state(TTS_STATE_SEARCHING);

    /* 3. Lookup. */
    const response_clip_t *clip = tts_find_response(response_text);
    if (clip == NULL) {
        tts_set_error(TTS_ERROR_RESPONSE_NOT_FOUND,
                      "tts_process: no matching response in database");
        return;
    }

    /* 4. Bind audio buffer. Flash pointer is used directly — no copy. */
    if (clip->audio_data == NULL || clip->audio_size == 0U) {
        tts_set_error(TTS_ERROR_AUDIO_LOAD_FAILED,
                      "tts_process: clip has empty audio payload");
        return;
    }

    /* 5. Validation + metadata. */
    tts_prepare_audio_for_playback(clip->audio_data, clip->audio_size);
    if (tts.state == TTS_STATE_ERROR) {
        /* prepare_for_playback already logged + set error */
        return;
    }

    /* If the clip carries an explicit duration, prefer it (it was computed
     * from the source .wav and avoids any rounding mismatch).               */
    if (clip->duration_ms != 0U) {
        tts.output_duration_ms = clip->duration_ms;
    }

    /* 6. Mark success. */
    tts.error_code = TTS_ERROR_NONE;
    tts.error_message[0] = '\0';
    tts_set_state(TTS_STATE_FOUND);

    /* 7. Bookkeeping. */
    tts.processing_duration_ms =
        system_get_tick_ms() - tts.processing_start_time;
    (void)strncpy(tts.last_processed_text, response_text,
                  TTS_LAST_TEXT_MAX - 1U);
    tts.last_processed_text[TTS_LAST_TEXT_MAX - 1U] = '\0';

    tts_logf("[tts] ready: %lu samples, %u ms (lookup took %u ms)",
             (unsigned long)tts.output_buffer_size,
             (unsigned)tts.output_duration_ms,
             (unsigned)tts.processing_duration_ms);
}

uint8_t tts_is_complete(void)
{
    return (tts.state == TTS_STATE_FOUND) ? 1U : 0U;
}

const int16_t *tts_get_audio_buffer(void)
{
    return tts.output_buffer;
}

uint32_t tts_get_audio_size(void)
{
    return tts.output_buffer_size;
}

uint16_t tts_get_audio_duration_ms(void)
{
    return tts.output_duration_ms;
}

uint8_t tts_get_error_code(void)
{
    return tts.error_code;
}

const char *tts_get_error_message(void)
{
    return tts.error_message;
}

void tts_cancel(void)
{
    tts.output_buffer      = NULL;
    tts.output_buffer_size = 0U;
    tts.output_duration_ms = 0U;
    tts_set_state(TTS_STATE_IDLE);
    tts_log("[tts] cancelled");
}
