/**
 * fsm.h — Cosmo Bot top-level finite state machine (STM32L476RG)
 * ============================================================================
 *
 * The FSM orchestrates one full conversational round-trip:
 *
 *   IDLE -> LISTENING -> PROCESSING_STT -> SENDING_TO_ESP32
 *        -> PROCESSING_TTS -> PLAYBACK -> IDLE
 *
 * Any state can fall through to ERROR on timeout / subsystem failure;
 * ERROR auto-returns to IDLE after a short display interval.
 *
 * All hardware I/O lives behind subsystem APIs (audio.h, stt.h, uart_comm.h,
 * tts.h, oled_display.h, system_utils.h). This file knows nothing about
 * pins, peripherals, or HAL — it only sequences the calls.
 *
 * ----------------------------------------------------------------------------
 * Usage:
 *
 *   fsm_init();                  // once, after HAL/clock/peripherals are up
 *   for (;;) {
 *       fsm_run();               // every ~10 ms
 *       system_delay_ms(10);
 *   }
 * ============================================================================
 */

#ifndef FSM_H
#define FSM_H

#include <stdint.h>

/* ========================================================================== */
/* States                                                                     */
/* ========================================================================== */

typedef enum {
    FSM_STATE_IDLE = 0,
    FSM_STATE_LISTENING,
    FSM_STATE_PROCESSING_STT,
    FSM_STATE_SENDING_TO_ESP32,
    FSM_STATE_PROCESSING_TTS,
    FSM_STATE_PLAYBACK,
    FSM_STATE_ERROR,
    FSM_STATE_COUNT  /* sentinel, not a real state */
} fsm_state_t;

/* ========================================================================== */
/* Error codes                                                                */
/* ========================================================================== */

#define FSM_ERROR_NONE                  0
#define FSM_ERROR_AUDIO_CAPTURE_FAILED  1
#define FSM_ERROR_STT_FAILED            2
#define FSM_ERROR_UART_TIMEOUT          3
#define FSM_ERROR_UART_ERROR            4
#define FSM_ERROR_TTS_FAILED            5
#define FSM_ERROR_PLAYBACK_FAILED       6
#define FSM_ERROR_UNKNOWN               99

/* ========================================================================== */
/* Buffer sizes                                                               */
/* ========================================================================== */

#define FSM_USER_INPUT_MAX      256
#define FSM_BOT_RESPONSE_MAX    512
#define FSM_ERROR_MSG_MAX       128

/* Per-state default timeouts (ms) — tweak as real subsystems come online. */
#define FSM_TIMEOUT_LISTENING_MS        6000U
#define FSM_TIMEOUT_PROCESSING_STT_MS   3000U
#define FSM_TIMEOUT_SENDING_ESP32_MS    5000U
#define FSM_TIMEOUT_PROCESSING_TTS_MS   3000U
#define FSM_TIMEOUT_PLAYBACK_MS        10000U
#define FSM_TIMEOUT_ERROR_MS            3000U

/* ========================================================================== */
/* FSM context                                                                */
/* ========================================================================== */

typedef struct {
    /* --- State --- */
    fsm_state_t current_state;
    fsm_state_t next_state;

    /* --- Audio capture / waveform --- */
    uint8_t  *audio_buffer;        /* raw mic audio (owned by audio subsystem) */
    uint16_t  audio_buffer_size;   /* bytes in audio_buffer                    */
    uint16_t *audio_samples;       /* downsampled samples for waveform         */
    uint16_t  audio_sample_count;  /* number of samples in audio_samples       */

    /* --- Text --- */
    char     user_input_text[FSM_USER_INPUT_MAX];   /* STT result              */
    char     bot_response_text[FSM_BOT_RESPONSE_MAX]; /* response from ESP32   */
    uint16_t response_text_length;

    /* --- TTS audio --- */
    uint8_t  *tts_audio_buffer;
    uint16_t  tts_audio_size;

    /* --- Timing --- */
    uint32_t state_entry_time;     /* tick (ms) when current state was entered */
    uint32_t timeout_ms;           /* timeout for current state (0 = none)     */
    uint16_t animation_frame;      /* counter for spinners / waveforms         */
    uint32_t last_anim_tick;       /* last tick we advanced animation_frame    */

    /* --- Errors --- */
    uint8_t  error_code;
    char     error_message[FSM_ERROR_MSG_MAX];

    /* --- UART link flags --- */
    uint8_t  uart_response_received;
    uint8_t  uart_timeout_occurred;

    /* --- Misc --- */
    uint8_t  should_exit;          /* set to 1 to break out of fsm_run() loops */
} fsm_context_t;

/* The global instance lives in fsm.c. */
extern fsm_context_t fsm;

/* ========================================================================== */
/* Helper macros                                                              */
/* ========================================================================== */

/**
 * FSM_CHECK_TIMEOUT(err_code, err_msg)
 * If the current state has exceeded fsm.timeout_ms, set the given error and
 * transition to FSM_STATE_ERROR, then `return;` from the calling function.
 * Use inside fsm_execute_*() handlers.
 */
#define FSM_CHECK_TIMEOUT(err_code, err_msg)             \
    do {                                                 \
        if (fsm_is_timeout()) {                          \
            fsm_set_error((err_code), (err_msg));        \
            return;                                      \
        }                                                \
    } while (0)

/* ========================================================================== */
/* Core FSM API                                                               */
/* ========================================================================== */

void fsm_init(void);
void fsm_run(void);
void fsm_update_state(void);
void fsm_execute_state(void);
void fsm_enter_state(fsm_state_t state);
void fsm_update_display(void);

/* ========================================================================== */
/* State entry handlers                                                       */
/* ========================================================================== */

void fsm_enter_idle(void);
void fsm_enter_listening(void);
void fsm_enter_processing_stt(void);
void fsm_enter_sending_to_esp32(void);
void fsm_enter_processing_tts(void);
void fsm_enter_playback(void);
void fsm_enter_error(void);

/* ========================================================================== */
/* State execution handlers                                                   */
/* ========================================================================== */

void fsm_execute_idle(void);
void fsm_execute_listening(void);
void fsm_execute_processing_stt(void);
void fsm_execute_sending_to_esp32(void);
void fsm_execute_processing_tts(void);
void fsm_execute_playback(void);
void fsm_execute_error(void);

/* ========================================================================== */
/* Display update handlers                                                    */
/* ========================================================================== */

void fsm_display_idle(void);
void fsm_display_listening(void);
void fsm_display_processing_stt(void);
void fsm_display_sending_to_esp32(void);
void fsm_display_processing_tts(void);
void fsm_display_playback(void);
void fsm_display_error(void);

/* ========================================================================== */
/* Error handling                                                             */
/* ========================================================================== */

void fsm_set_error(uint8_t error_code, const char *error_message);

/* ========================================================================== */
/* Utilities                                                                  */
/* ========================================================================== */

uint32_t    fsm_get_state_duration_ms(void);
uint8_t     fsm_is_timeout(void);
void        fsm_set_state_timeout(uint32_t timeout_ms);
const char *fsm_state_to_string(fsm_state_t state);

#endif /* FSM_H */
