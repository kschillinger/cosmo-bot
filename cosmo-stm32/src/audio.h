/**
 * @file    audio.h
 * @brief   PDM microphone capture pipeline — public API.
 *
 * Captures speech audio from a MEMS PDM microphone (e.g. ICS-43432) into a
 * circular RAM buffer via the STM32L476 SAI peripheral + DMA, performs
 * RMS-based silence detection, and produces a downsampled waveform for the
 * OLED display.
 *
 * Layered design (matches Phase-2 spec):
 *   Layer 1 — HAL abstraction      (audio_hal_*)
 *   Layer 2 — Circular buffer mgmt (audio_buffer_*)
 *   Layer 3 — Sound detection      (audio_compute_rms / silence)
 *   Layer 4 — Waveform extraction  (audio_extract_waveform)
 *   Layer 5 — Public API           (audio_init, audio_capture_start, …)
 *
 * Lifecycle:
 *   audio_init();                    // once, at boot
 *   audio_capture_start();           // FSM enters LISTENING
 *   …
 *   if (audio_capture_is_auto_stop_triggered()) {
 *       audio_capture_stop();        // FSM leaves LISTENING
 *       buf  = audio_get_buffer();
 *       size = audio_get_buffer_size();
 *       // hand (buf,size) to STT
 *   }
 *
 * Memory budget (RAM):
 *   audio_buf.buffer ............ 64 KB (32000 × int16_t)
 *   waveform.samples ............ 256 B
 *   capture_state ............... ~32 B
 *   ─────────────────────────────────────
 *   total ....................... ~64 KB  (fits in 128 KB STM32L476 SRAM)
 *
 * Note on PDM:
 *   The SAI peripheral delivers raw PDM bits; on real hardware a decimation
 *   filter (CMSIS-DSP `arm_pdm_to_pcm` or the DFSDM peripheral) is required
 *   to obtain 16-bit PCM. This driver assumes SAI is configured to deliver
 *   16-bit PCM samples directly (either via DFSDM-front-end or by chaining
 *   the PDM filter before the buffer write). The HAL layer hides that.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Compile-time configuration                                                */
/* ========================================================================== */

/** Audio sample rate in Hz. 16 kHz is plenty for speech recognition. */
#define AUDIO_SAMPLE_RATE               16000U

/** Maximum capture duration in seconds. Bounded by SRAM, not by DMA. */
#define AUDIO_CAPTURE_DURATION_MAX_S    2U

/** Total samples held in the circular buffer.            (= 32 000) */
#define AUDIO_BUFFER_SIZE               (AUDIO_SAMPLE_RATE * AUDIO_CAPTURE_DURATION_MAX_S)

/** Total bytes held in the circular buffer.              (= 64 KiB) */
#define AUDIO_BUFFER_SIZE_BYTES         (AUDIO_BUFFER_SIZE * sizeof(audio_sample_t))

/** Bins in the downsampled waveform — matches OLED width in pixels. */
#define WAVEFORM_SAMPLE_COUNT           128U

/** Waveform refresh rate in Hz. 10 Hz = smooth, low-CPU animation. */
#define WAVEFORM_UPDATE_RATE_HZ         10U

/** Period between waveform updates in ms.                (= 100 ms) */
#define WAVEFORM_UPDATE_PERIOD_MS       (1000U / WAVEFORM_UPDATE_RATE_HZ)

/** Sample window analysed for RMS / silence each tick.   (= 100 ms) */
#define AUDIO_RMS_WINDOW_SAMPLES        (AUDIO_SAMPLE_RATE / WAVEFORM_UPDATE_RATE_HZ)

/** Default RMS threshold below which audio counts as silence. Tunable. */
#define AUDIO_DEFAULT_SILENCE_THRESHOLD 500U

/** Default auto-stop timeout (ms of continuous silence). */
#define AUDIO_DEFAULT_AUTOSTOP_MS       2000U

/* ========================================================================== */
/*  Types                                                                     */
/* ========================================================================== */

/** One PCM sample. Signed 16-bit, native endianness. */
typedef int16_t audio_sample_t;

/**
 * Circular capture buffer. Written by DMA, read by the STT subsystem.
 * `write_index` is updated from interrupt context — keep volatile.
 */
typedef struct {
    audio_sample_t buffer[AUDIO_BUFFER_SIZE]; /**< raw 16-bit PCM        */
    volatile uint32_t write_index;            /**< next sample DMA writes */
    uint32_t          read_index;             /**< oldest unread sample   */
    volatile uint32_t sample_count;           /**< total samples this run */
    volatile uint8_t  overflow;               /**< 1 = wrapped before read*/
} audio_buffer_t;

/**
 * Downsampled peak-envelope waveform for the display.
 * Touched from both the timer ISR (writer) and the display loop (reader);
 * single-word atomicity is enough for the `valid` flag.
 */
typedef struct {
    uint16_t samples[WAVEFORM_SAMPLE_COUNT];  /**< 0 … 65535, peak per bin */
    uint32_t last_update_time_ms;
    volatile uint8_t valid;
} waveform_buffer_t;

/** High-level capture state, driven by the FSM. */
typedef enum {
    AUDIO_STATE_IDLE = 0,
    AUDIO_STATE_RECORDING,
    AUDIO_STATE_DONE
} audio_state_t;

typedef struct {
    volatile audio_state_t state;
    uint32_t recording_start_time_ms;
    uint32_t recording_duration_ms;
    volatile uint32_t silence_duration_ms;
    volatile uint8_t  sound_detected;
    volatile uint16_t audio_level_rms;
    volatile uint16_t audio_level_peak;
    uint16_t silence_threshold;
    uint32_t auto_stop_timeout_ms;
} audio_capture_state_t;

/* ========================================================================== */
/*  Globals (defined in audio.c)                                              */
/* ========================================================================== */

extern audio_buffer_t        audio_buf;
extern waveform_buffer_t     waveform;
extern audio_capture_state_t capture_state;

/* ========================================================================== */
/*  Layer 1 — HAL abstraction                                                 */
/* ========================================================================== */

void audio_hal_init(void);
void audio_hal_start_dma(void);
void audio_hal_stop_dma(void);
void audio_hal_get_dma_write_index(uint32_t *index);

/* ========================================================================== */
/*  Layer 2 — Circular buffer management                                      */
/* ========================================================================== */

void              audio_buffer_init(void);
void              audio_buffer_reset(void);
uint32_t          audio_buffer_get_write_index(void);
void              audio_buffer_set_write_index(uint32_t index);
uint32_t          audio_buffer_get_sample_count(void);
audio_sample_t   *audio_buffer_get_ptr(uint32_t offset);

/* ========================================================================== */
/*  Layer 3 — Sound detection                                                 */
/* ========================================================================== */

uint16_t audio_compute_rms (uint32_t start_index, uint32_t sample_count);
uint16_t audio_compute_peak(uint32_t start_index, uint32_t sample_count);
uint8_t  audio_is_silent   (uint16_t audio_level, uint16_t threshold);
void     audio_update_sound_detection(void);
void     audio_set_silence_threshold(uint16_t threshold);

/* ========================================================================== */
/*  Layer 4 — Waveform extraction                                             */
/* ========================================================================== */

void      audio_extract_waveform(void);
uint16_t *audio_get_waveform_samples(void);
uint16_t  audio_get_waveform_sample_count(void);
uint8_t   audio_is_waveform_valid(void);

/* ========================================================================== */
/*  Layer 5 — Public API consumed by the FSM                                  */
/* ========================================================================== */

void              audio_init(void);
void              audio_capture_start(void);
void              audio_capture_stop(void);
uint8_t           audio_capture_is_active(void);
uint8_t           audio_capture_is_auto_stop_triggered(void);
audio_sample_t   *audio_get_buffer(void);
uint32_t          audio_get_buffer_size(void);
uint16_t         *audio_get_samples(void);
uint16_t          audio_get_sample_count(void);
uint8_t           audio_is_sound_detected(void);
uint16_t          audio_get_level_rms(void);
uint16_t          audio_get_level_peak(void);
uint32_t          audio_get_recording_duration_ms(void);

/* ========================================================================== */
/*  External hooks                                                            */
/* ========================================================================== */

/**
 * Provided elsewhere in the project (debug_uart.c / system tick).
 * Returns a free-running millisecond counter, monotonically increasing.
 */
extern uint32_t system_get_tick_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
