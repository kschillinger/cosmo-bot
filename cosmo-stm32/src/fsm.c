/**
 * fsm.c — Cosmo Bot top-level FSM implementation
 * ============================================================================
 * See fsm.h for the architecture overview.
 *
 * Conventions:
 *   - All hardware I/O goes through subsystem APIs (audio_*, stt_*, etc.).
 *   - State transitions happen by writing fsm.next_state; the entry handler
 *     for the new state runs once on the next fsm_run() iteration.
 *   - Every handler logs via printf() to USART2 (ST-Link VCP) for tracing.
 * ============================================================================
 */

#include "fsm.h"

#include "audio.h"
#include "stt.h"
#include "uart_comm.h"
#include "tts.h"
#include "oled_display.h"
#include "system_utils.h"

#include <stdio.h>
#include <string.h>

/* ========================================================================== */
/* Global FSM context                                                         */
/* ========================================================================== */

fsm_context_t fsm;

/* Animation frame interval — controls spinner / waveform redraw rate. */
#define FSM_ANIM_INTERVAL_MS    100U

/* ========================================================================== */
/* Forward decls (file-local helpers)                                         */
/* ========================================================================== */

static void fsm__safe_strncpy(char *dst, const char *src, size_t dst_size);

/* ========================================================================== */
/* Core FSM functions                                                         */
/* ========================================================================== */

/**
 * Initialize FSM state and bring up every subsystem.
 * Call once after HAL/clock/peripherals are configured.
 */
void fsm_init(void)
{
    printf("[FSM] fsm_init()\r\n");

    /* Zero the entire context — guarantees buffers, flags, and counters
     * start from a known state. */
    memset(&fsm, 0, sizeof(fsm));

    /* Bring up every subsystem. Order is mostly independent — audio
     * before stt before uart before tts before oled — but tts/audio
     * could later share a DAC peripheral. */
    audio_init();
    stt_init();
    uart_init();
    tts_init();
    oled_init();

    /* Force a clean entry into IDLE so the entry handler runs. */
    fsm.current_state    = FSM_STATE_COUNT;   /* sentinel != IDLE          */
    fsm.next_state       = FSM_STATE_IDLE;
    fsm.state_entry_time = system_get_tick_ms();
    fsm.last_anim_tick   = fsm.state_entry_time;

    printf("[FSM] init complete; entering IDLE\r\n");
}

/**
 * One pass of the FSM. Call repeatedly from main loop (~10 ms cadence).
 *   1. Apply any pending state transition
 *   2. Run the current state's logic
 *   3. Refresh the display
 */
void fsm_run(void)
{
    fsm_update_state();
    fsm_execute_state();
    fsm_update_display();
}

/**
 * If next_state differs from current_state, transition into it.
 * The entry handler runs exactly once per transition.
 */
void fsm_update_state(void)
{
    if (fsm.current_state == fsm.next_state) {
        return;  /* no transition pending */
    }

    printf("[%lums] [FSM] %s -> %s\r\n",
           (unsigned long)system_get_tick_ms(),
           fsm_state_to_string(fsm.current_state),
           fsm_state_to_string(fsm.next_state));

    fsm_enter_state(fsm.next_state);
}

/**
 * Dispatch to the current state's execution handler.
 */
void fsm_execute_state(void)
{
    switch (fsm.current_state) {
        case FSM_STATE_IDLE:             fsm_execute_idle();             break;
        case FSM_STATE_LISTENING:        fsm_execute_listening();        break;
        case FSM_STATE_PROCESSING_STT:   fsm_execute_processing_stt();   break;
        case FSM_STATE_SENDING_TO_ESP32: fsm_execute_sending_to_esp32(); break;
        case FSM_STATE_PROCESSING_TTS:   fsm_execute_processing_tts();   break;
        case FSM_STATE_PLAYBACK:         fsm_execute_playback();         break;
        case FSM_STATE_ERROR:            fsm_execute_error();            break;
        default:
            printf("[FSM] unknown state %d, resetting to IDLE\r\n",
                   (int)fsm.current_state);
            fsm.next_state = FSM_STATE_IDLE;
            break;
    }
}

/**
 * Common transition bookkeeping plus dispatch to the per-state entry handler.
 */
void fsm_enter_state(fsm_state_t state)
{
    fsm.current_state    = state;
    fsm.state_entry_time = system_get_tick_ms();
    fsm.last_anim_tick   = fsm.state_entry_time;
    fsm.animation_frame  = 0;
    fsm.next_state       = state;  /* clear pending transition */

    switch (state) {
        case FSM_STATE_IDLE:             fsm_enter_idle();             break;
        case FSM_STATE_LISTENING:        fsm_enter_listening();        break;
        case FSM_STATE_PROCESSING_STT:   fsm_enter_processing_stt();   break;
        case FSM_STATE_SENDING_TO_ESP32: fsm_enter_sending_to_esp32(); break;
        case FSM_STATE_PROCESSING_TTS:   fsm_enter_processing_tts();   break;
        case FSM_STATE_PLAYBACK:         fsm_enter_playback();         break;
        case FSM_STATE_ERROR:            fsm_enter_error();            break;
        default: break;
    }
}

/**
 * Advance the animation frame counter (rate-limited) and call the per-state
 * display routine. The OLED redraw cadence is decoupled from fsm_run() so
 * spinners look smooth regardless of main-loop period.
 */
void fsm_update_display(void)
{
    uint32_t now = system_get_tick_ms();
    if ((now - fsm.last_anim_tick) >= FSM_ANIM_INTERVAL_MS) {
        fsm.animation_frame++;
        fsm.last_anim_tick = now;
    }

    switch (fsm.current_state) {
        case FSM_STATE_IDLE:             fsm_display_idle();             break;
        case FSM_STATE_LISTENING:        fsm_display_listening();        break;
        case FSM_STATE_PROCESSING_STT:   fsm_display_processing_stt();   break;
        case FSM_STATE_SENDING_TO_ESP32: fsm_display_sending_to_esp32(); break;
        case FSM_STATE_PROCESSING_TTS:   fsm_display_processing_tts();   break;
        case FSM_STATE_PLAYBACK:         fsm_display_playback();         break;
        case FSM_STATE_ERROR:            fsm_display_error();            break;
        default: break;
    }
}

/* ========================================================================== */
/* State entry handlers                                                       */
/* ========================================================================== */

void fsm_enter_idle(void)
{
    printf("[FSM] enter IDLE\r\n");
    fsm.timeout_ms = 0;  /* IDLE never times out */
    fsm.error_code = FSM_ERROR_NONE;
    fsm.error_message[0] = '\0';
}

void fsm_enter_listening(void)
{
    printf("[FSM] enter LISTENING\r\n");
    fsm.timeout_ms = FSM_TIMEOUT_LISTENING_MS;

    audio_capture_start();
    fsm.audio_buffer      = NULL;
    fsm.audio_buffer_size = 0;
}

void fsm_enter_processing_stt(void)
{
    printf("[FSM] enter PROCESSING_STT\r\n");
    fsm.timeout_ms = FSM_TIMEOUT_PROCESSING_STT_MS;

    /* Hand the captured audio to the STT engine. */
    fsm.audio_buffer      = audio_get_buffer();
    fsm.audio_buffer_size = audio_get_buffer_size();
    stt_process(fsm.audio_buffer, fsm.audio_buffer_size);
}

void fsm_enter_sending_to_esp32(void)
{
    printf("[FSM] enter SENDING_TO_ESP32  txt=\"%s\"\r\n", fsm.user_input_text);
    fsm.timeout_ms              = FSM_TIMEOUT_SENDING_ESP32_MS;
    fsm.uart_response_received  = 0;
    fsm.uart_timeout_occurred   = 0;

    uart_send_message(fsm.user_input_text);
}

void fsm_enter_processing_tts(void)
{
    printf("[FSM] enter PROCESSING_TTS  txt=\"%s\"\r\n", fsm.bot_response_text);
    fsm.timeout_ms = FSM_TIMEOUT_PROCESSING_TTS_MS;

    tts_process(fsm.bot_response_text);
}

void fsm_enter_playback(void)
{
    printf("[FSM] enter PLAYBACK\r\n");
    fsm.timeout_ms = FSM_TIMEOUT_PLAYBACK_MS;

    fsm.tts_audio_buffer = tts_get_audio_buffer();
    fsm.tts_audio_size   = tts_get_audio_size();
    audio_play_buffer(fsm.tts_audio_buffer, fsm.tts_audio_size);
}

void fsm_enter_error(void)
{
    printf("[FSM] enter ERROR  code=%u msg=\"%s\"\r\n",
           (unsigned)fsm.error_code, fsm.error_message);
    fsm.timeout_ms = FSM_TIMEOUT_ERROR_MS;

    oled_display_error(fsm.error_message);
}

/* ========================================================================== */
/* State execution handlers                                                   */
/* ========================================================================== */

/**
 * IDLE: wait for a wake event. For now this is a button edge on B1 (PC13);
 * later it'll also fire on a sound-amplitude threshold from the mic.
 */
void fsm_execute_idle(void)
{
    if (system_button_pressed_edge()) {
        printf("[FSM] IDLE: button edge -> LISTENING\r\n");
        fsm.next_state = FSM_STATE_LISTENING;
        return;
    }

    /* TODO: when the mic subsystem is real, also check
     *       audio_amplitude_above_threshold(). */
}

/**
 * LISTENING: the audio subsystem is filling its capture buffer; we just
 * wait for it to report "done" (or for the timeout to fire).
 */
void fsm_execute_listening(void)
{
    FSM_CHECK_TIMEOUT(FSM_ERROR_AUDIO_CAPTURE_FAILED, "Audio capture timeout");

    if (!audio_capture_is_active()) {
        audio_capture_stop();
        fsm.next_state = FSM_STATE_PROCESSING_STT;
        return;
    }
    /* still recording — display handler refreshes the waveform */
}

/**
 * PROCESSING_STT: poll the STT engine for completion, copy the result.
 */
void fsm_execute_processing_stt(void)
{
    FSM_CHECK_TIMEOUT(FSM_ERROR_STT_FAILED, "STT timeout");

    if (stt_is_complete()) {
        const char *result = stt_get_result();
        if (result == NULL || result[0] == '\0') {
            fsm_set_error(FSM_ERROR_STT_FAILED, "STT empty result");
            return;
        }
        fsm__safe_strncpy(fsm.user_input_text, result, sizeof(fsm.user_input_text));
        printf("[FSM] STT -> \"%s\"\r\n", fsm.user_input_text);
        fsm.next_state = FSM_STATE_SENDING_TO_ESP32;
    }
}

/**
 * SENDING_TO_ESP32: poll the UART link for a complete BOT_RESPONSE line.
 */
void fsm_execute_sending_to_esp32(void)
{
    FSM_CHECK_TIMEOUT(FSM_ERROR_UART_TIMEOUT, "ESP32 UART timeout");

    if (uart_message_available()) {
        const char *response = uart_receive_message();
        if (response == NULL || response[0] == '\0') {
            fsm_set_error(FSM_ERROR_UART_ERROR, "ESP32 empty response");
            return;
        }
        fsm__safe_strncpy(fsm.bot_response_text, response,
                          sizeof(fsm.bot_response_text));
        fsm.response_text_length   = (uint16_t)strlen(fsm.bot_response_text);
        fsm.uart_response_received = 1;
        printf("[FSM] ESP32 -> \"%s\"\r\n", fsm.bot_response_text);
        fsm.next_state = FSM_STATE_PROCESSING_TTS;
    }
}

/**
 * PROCESSING_TTS: wait for the TTS engine to render the response.
 */
void fsm_execute_processing_tts(void)
{
    FSM_CHECK_TIMEOUT(FSM_ERROR_TTS_FAILED, "TTS timeout");

    if (tts_is_complete()) {
        uint8_t *buf  = tts_get_audio_buffer();
        uint16_t size = tts_get_audio_size();
        if (buf == NULL || size == 0) {
            fsm_set_error(FSM_ERROR_TTS_FAILED, "TTS empty audio");
            return;
        }
        fsm.next_state = FSM_STATE_PLAYBACK;
    }
}

/**
 * PLAYBACK: wait for the speaker DMA / playback engine to finish.
 * On timeout we just fall back to IDLE (don't escalate to ERROR — the
 * audio was already produced; we just took too long playing it).
 */
void fsm_execute_playback(void)
{
    if (fsm_is_timeout()) {
        printf("[FSM] PLAYBACK: timeout, returning to IDLE\r\n");
        fsm.next_state = FSM_STATE_IDLE;
        return;
    }

    if (audio_playback_is_done()) {
        fsm.next_state = FSM_STATE_IDLE;
    }
}

/**
 * ERROR: hold the error display for FSM_TIMEOUT_ERROR_MS, then return home.
 */
void fsm_execute_error(void)
{
    if (fsm_is_timeout()) {
        printf("[FSM] ERROR: display interval elapsed, returning to IDLE\r\n");
        fsm.next_state = FSM_STATE_IDLE;
    }
}

/* ========================================================================== */
/* Display update handlers                                                    */
/* ========================================================================== */

void fsm_display_idle(void)
{
    oled_display_idle();
}

void fsm_display_listening(void)
{
    fsm.audio_samples      = audio_get_samples();
    fsm.audio_sample_count = audio_get_sample_count();
    oled_display_listening(fsm.audio_samples, fsm.audio_sample_count);
}

void fsm_display_processing_stt(void)
{
    oled_display_processing(fsm.animation_frame);
}

void fsm_display_sending_to_esp32(void)
{
    /* Same spinner as STT processing, but the recognized text is also
     * available for the OLED layer to render under the spinner. */
    oled_display_processing(fsm.animation_frame);
}

void fsm_display_processing_tts(void)
{
    oled_display_processing(fsm.animation_frame);
}

void fsm_display_playback(void)
{
    oled_display_responding(fsm.bot_response_text, fsm.animation_frame);
}

void fsm_display_error(void)
{
    /* Static — entry handler already pushed the message; nothing to animate. */
}

/* ========================================================================== */
/* Error handling                                                             */
/* ========================================================================== */

/**
 * Record an error and queue a transition to FSM_STATE_ERROR.
 * Safe to call from any execution handler.
 */
void fsm_set_error(uint8_t error_code, const char *error_message)
{
    fsm.error_code = error_code;
    fsm__safe_strncpy(fsm.error_message,
                      error_message ? error_message : "(null)",
                      sizeof(fsm.error_message));
    printf("[FSM] !! error %u: %s\r\n",
           (unsigned)error_code, fsm.error_message);
    fsm.next_state = FSM_STATE_ERROR;
}

/* ========================================================================== */
/* Utility functions                                                          */
/* ========================================================================== */

uint32_t fsm_get_state_duration_ms(void)
{
    return system_get_tick_ms() - fsm.state_entry_time;
}

uint8_t fsm_is_timeout(void)
{
    if (fsm.timeout_ms == 0) return 0;  /* 0 means "no timeout" */
    return (fsm_get_state_duration_ms() > fsm.timeout_ms) ? 1U : 0U;
}

void fsm_set_state_timeout(uint32_t timeout_ms)
{
    fsm.timeout_ms = timeout_ms;
}

const char *fsm_state_to_string(fsm_state_t state)
{
    switch (state) {
        case FSM_STATE_IDLE:             return "IDLE";
        case FSM_STATE_LISTENING:        return "LISTENING";
        case FSM_STATE_PROCESSING_STT:   return "PROCESSING_STT";
        case FSM_STATE_SENDING_TO_ESP32: return "SENDING_TO_ESP32";
        case FSM_STATE_PROCESSING_TTS:   return "PROCESSING_TTS";
        case FSM_STATE_PLAYBACK:         return "PLAYBACK";
        case FSM_STATE_ERROR:            return "ERROR";
        default:                         return "?";
    }
}

/* ========================================================================== */
/* File-local helpers                                                         */
/* ========================================================================== */

static void fsm__safe_strncpy(char *dst, const char *src, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}
