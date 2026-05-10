/**
 * @file    tts.h
 * @brief   Text-to-Speech subsystem for STM32L476 (Cosmo chatbot)
 *
 * MVP approach: pre-recorded clips stored in Flash. Given a response text from
 * the dialogue engine, the TTS layer looks up a matching audio clip and
 * exposes a pointer + length that the audio playback subsystem can consume
 * directly (no copy, no allocation).
 *
 * Audio format (all clips):
 *   - 16-bit signed PCM
 *   - 16 kHz sample rate
 *   - mono
 *
 * Performance targets:
 *   - tts_process()  : < 100 ms (Flash lookup, no I/O)
 *   - RAM footprint  : < 4 KB   (context + working state only)
 */

#ifndef COSMO_TTS_H
#define COSMO_TTS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ────────────────────────── Compile-time configuration ─────────────────── */

#define TTS_AUDIO_SAMPLE_RATE_HZ   16000U
#define TTS_AUDIO_BITS_PER_SAMPLE  16U
#define TTS_AUDIO_CHANNELS         1U

/* String buffer sizes (kept modest to bound RAM use). */
#define TTS_ERROR_MESSAGE_MAX      128U
#define TTS_LAST_TEXT_MAX          512U

/* Sanity bounds applied during prepare-for-playback. */
#define TTS_PEAK_QUIET_THRESHOLD   500
#define TTS_PEAK_LOUD_THRESHOLD    30000

/* ────────────────────────── Error codes ────────────────────────────────── */

#define TTS_ERROR_NONE                 0U
#define TTS_ERROR_RESPONSE_NOT_FOUND   1U
#define TTS_ERROR_AUDIO_LOAD_FAILED    2U
#define TTS_ERROR_INVALID_INPUT        3U
#define TTS_ERROR_BUFFER_OVERFLOW      4U
#define TTS_ERROR_TIMEOUT              5U

/* ────────────────────────── Types ──────────────────────────────────────── */

/** Top-level TTS state, queried by the FSM. */
typedef enum {
    TTS_STATE_IDLE      = 0,  /**< Not processing, no output ready.       */
    TTS_STATE_SEARCHING = 1,  /**< Database lookup in progress.           */
    TTS_STATE_FOUND     = 2,  /**< Match found; output buffer is valid.   */
    TTS_STATE_ERROR     = 3   /**< Last operation failed; see error_code. */
} tts_state_t;

/**
 * One row in the response database: a piece of response text bound to its
 * pre-recorded PCM payload (which lives in Flash).
 */
typedef struct {
    const char     *response_text;     /**< Exact text from dialogue engine. */
    const int16_t  *audio_data;        /**< Pointer to PCM samples in Flash. */
    uint32_t        audio_size;        /**< Number of int16_t samples.       */
    uint16_t        duration_ms;       /**< Playback duration (ms).          */
    const char     *source_filename;   /**< Original .wav path (for debug).  */
} response_clip_t;

/**
 * Runtime state of the TTS subsystem. Single global instance lives in tts.c.
 * Members are read-only to callers (use the accessor functions below).
 */
typedef struct {
    tts_state_t      state;
    const int16_t   *output_buffer;
    uint32_t         output_buffer_size;
    uint16_t         output_duration_ms;

    uint8_t          error_code;
    char             error_message[TTS_ERROR_MESSAGE_MAX];

    uint32_t         processing_start_time;
    uint32_t         processing_duration_ms;
    char             last_processed_text[TTS_LAST_TEXT_MAX];
} tts_context_t;

/* ────────────────────────── Externs (defined in tts.c / db file) ───────── */

extern tts_context_t          tts;
extern const response_clip_t  response_database[];
extern const uint32_t         response_database_size;

/* ────────────────────────── Public API ─────────────────────────────────── */

/**
 * Initialise the TTS subsystem. Must be called once from main() before any
 * other tts_* function. Idempotent.
 */
void tts_init(void);

/**
 * Look up @p response_text in the database, bind the matching clip as the
 * current output, and transition to TTS_STATE_FOUND on success or
 * TTS_STATE_ERROR on failure. Returns synchronously; on a Cortex-M4 @ 80 MHz
 * with a small database this completes in well under 100 ms.
 *
 * @param response_text  NUL-terminated UTF-8 string from the dialogue engine.
 *                       NULL or empty is treated as TTS_ERROR_INVALID_INPUT.
 */
void tts_process(const char *response_text);

/** @return 1 if the output buffer is valid and ready for playback, else 0. */
uint8_t tts_is_complete(void);

/** @return Pointer to PCM samples in Flash, or NULL if no output is ready. */
const int16_t *tts_get_audio_buffer(void);

/** @return Number of int16_t samples in the output buffer. */
uint32_t tts_get_audio_size(void);

/** @return Duration of the output buffer in milliseconds. */
uint16_t tts_get_audio_duration_ms(void);

/** @return Last error code (TTS_ERROR_NONE if no error). */
uint8_t tts_get_error_code(void);

/** @return Human-readable description of the last error. */
const char *tts_get_error_message(void);

/**
 * Cancel any in-flight processing and reset the output buffer pointers.
 * Safe to call from any state.
 */
void tts_cancel(void);

/* ────────────────────────── Internal helpers ───────────────────────────── *
 * Exposed primarily for unit tests; production code should stick to the
 * public API above.
 */

const response_clip_t *tts_find_response(const char *response_text);

uint8_t tts_select_response_variant(const char           *intent_prefix,
                                    const response_clip_t **out_clip);

void    tts_prepare_audio_for_playback(const int16_t *audio_data,
                                       uint32_t       sample_count);

void    tts_set_state(tts_state_t new_state);
void    tts_set_error(uint8_t error_code, const char *error_message);

#ifdef __cplusplus
}
#endif

#endif /* COSMO_TTS_H */
