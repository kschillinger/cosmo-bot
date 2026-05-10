/*
 * audio_playback.h — DAC-based audio playback for cosmo-bot
 *
 * Target:   STM32L476 @ 80 MHz
 * Output:   DAC1 channel 1 (PA4) → PAM8302 class-D amp → 8 Ω speaker
 * Trigger:  TIM6 TRGO @ 16 kHz drives the DAC sample clock
 * Transfer: DMA2 Channel 4 (request 3, DAC1_CH1) in circular ping-pong mode
 * Format:   16-bit signed mono PCM in, 12-bit unsigned right-aligned out
 *
 *   Why ping-pong rather than a single big DMA buffer?
 *   ----------------------------------------------------
 *   A 3-second TTS clip at 16 kHz is 48 000 samples. Converted to 12-bit
 *   half-words that's 96 kB — three quarters of the L476's 128 kB SRAM.
 *   Instead, we allocate one small (1024-sample, 2 kB) circular DAC ring,
 *   keep the 16-bit source as-is in the caller's buffer, and refill one
 *   half of the ring at every half-transfer / transfer-complete interrupt
 *   while the DAC plays the other half. End-of-clip is detected by tracking
 *   how many source samples remain; the last half is padded with mid-scale
 *   silence and DMA is stopped on the final TC interrupt.
 *
 * Layered design (matches the audio-subsystem spec):
 *   Layer 1 — Hardware abstraction: DAC, TIM6, DMA, amp-shutdown GPIO
 *   Layer 2 — Buffer prep: 16→12-bit conversion, gain, peak detection
 *   Layer 3 — Playback control: play / stop / done / progress
 *   Layer 4 — Volume control: set / get / mute / unmute / amp shutdown
 *   Layer 5 — Public API consumed by the FSM
 *
 * Memory footprint:
 *   DAC ring buffer:   2 048 B (static, .bss)
 *   Playback context:  ~ 64 B
 *   Code:              ~ 4 kB Flash (-Os)
 */

#ifndef AUDIO_PLAYBACK_H
#define AUDIO_PLAYBACK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Compile-time configuration                                                */
/* ========================================================================== */

/* Audio format. Must match microphone capture and TTS synthesis subsystems. */
#define AUDIO_SAMPLE_RATE_HZ        16000U
#define AUDIO_BITS_PER_SAMPLE       16

/* DAC ring buffer. Power of two, large enough that one half plays for
 * ≥ 16 ms — comfortably more than the worst-case ISR latency budget. At
 * 16 kHz, 512 samples = 32 ms per half-buffer.                             */
#define AUDIO_DMA_RING_SAMPLES      1024U
#define AUDIO_DMA_HALF_SAMPLES      (AUDIO_DMA_RING_SAMPLES / 2U)

/* DAC midpoint for 12-bit right-aligned output. A 0-valued PCM sample maps
 * here, which is also the "silence" / amp-quiet level (1.65 V on a 3.3 V
 * supply).                                                                 */
#define AUDIO_DAC_MID_SCALE         2048U
#define AUDIO_DAC_MAX_VALUE         4095U

/* Default startup volume (percent). The PAM8302 has fixed analog gain so
 * we leave headroom rather than running flat-out from boot.                */
#define AUDIO_DEFAULT_VOLUME        75U

/* Hard maximum software gain. Anything above 1.0 will clip — exposed so
 * the caller can opt in to ±10 dB if they really want it.                  */
#define AUDIO_MAX_GAIN              2.0f

/* Amplifier shutdown GPIO (PAM8302 SD pin). Active-low: drive HIGH to run. */
#define AUDIO_AMP_SHUTDOWN_PORT     GPIOB
#define AUDIO_AMP_SHUTDOWN_PIN      GPIO_PIN_2

/* ========================================================================== */
/*  Types                                                                     */
/* ========================================================================== */

/* 16-bit signed PCM is the canonical sample format throughout the pipeline. */
typedef int16_t audio_sample_t;

typedef enum {
    AUDIO_PLAYBACK_IDLE = 0,    /* nothing queued, DMA stopped              */
    AUDIO_PLAYBACK_RUNNING,     /* DMA active, samples still feeding        */
    AUDIO_PLAYBACK_PAUSED,      /* DMA stopped mid-buffer, position kept    */
    AUDIO_PLAYBACK_DONE         /* clip finished, awaiting next command     */
} audio_playback_state_t;

typedef enum {
    AUDIO_OK = 0,
    AUDIO_ERR_NULL_BUFFER = -1, /* buffer pointer was NULL                  */
    AUDIO_ERR_EMPTY_BUFFER = -2,/* size_samples was 0                       */
    AUDIO_ERR_HARDWARE = -3,    /* HAL_* call returned non-OK               */
    AUDIO_ERR_NOT_INITIALISED = -4
} audio_status_t;

/* ========================================================================== */
/*  Layer 5 — Public API (called by FSM, TTS, app code)                       */
/* ========================================================================== */

/* One-shot init. Configures DAC, TIM6, DMA, amp GPIO, sets default volume.
 * Idempotent: safe to call again, but you almost certainly don't want to.   */
void audio_init(void);

/* Begin playback of a 16-bit PCM mono buffer at AUDIO_SAMPLE_RATE_HZ.
 * Returns immediately (non-blocking) — DMA does the work. If a clip is
 * already playing, it is stopped first.
 *
 * The caller MUST keep `buffer` alive until audio_playback_is_done() returns
 * 1 or audio_playback_stop() is called. The driver does not copy the source. */
audio_status_t audio_play_buffer(const audio_sample_t *buffer,
                                  uint32_t size_samples);

/* Halt playback immediately. Optionally puts the amp into shutdown to save
 * power (it draws ~1 mA quiescent when enabled).                           */
void audio_playback_stop(void);

/* Non-blocking poll. Returns 1 once the final source sample has been
 * pushed into the DAC; 0 otherwise (idle, paused, or still running).      */
uint8_t audio_playback_is_done(void);

/* 0 .. 100. Snapshots the current source-buffer cursor. Returns 0 when
 * idle/paused/done.                                                        */
uint32_t audio_playback_get_progress_percent(void);

/* Inspector for the FSM debug logger. */
audio_playback_state_t audio_playback_get_state(void);

/* Volume in percent (0 mute, 100 unity). Takes effect on the *next* DAC
 * ring refill, so a glitch-free crossfade of < 32 ms.                     */
void    audio_set_volume(uint8_t volume_percent);
uint8_t audio_get_volume(void);

/* Mute remembers the previous volume; unmute restores it. */
void    audio_mute(void);
void    audio_unmute(void);
uint8_t audio_is_muted(void);

/* Direct amp power control. shutdown=1 puts PAM8302 in low-power mode
 * (typ. 0.5 µA). Use during long idle periods to extend battery life.     */
void audio_amplifier_shutdown(uint8_t shutdown);

/* ========================================================================== */
/*  Layer 2 helpers (exposed for unit testing on host & for visibility)       */
/* ========================================================================== */

/* PCM signed 16-bit -> DAC unsigned 12-bit right-aligned.
 *   in[i] = -32768 -> out[i] = 0
 *   in[i] = 0      -> out[i] = 2048
 *   in[i] = +32767 -> out[i] = 4095                                        */
void audio_convert_16bit_to_12bit(const audio_sample_t *in,
                                   uint32_t sample_count,
                                   uint16_t *out);

/* In-place gain with saturating clip to int16_t range. */
void audio_apply_gain(audio_sample_t *samples,
                      uint32_t sample_count,
                      float gain);

/* Returns max |sample| seen, used for clipping detection. */
uint16_t audio_compute_peak_level(const audio_sample_t *samples,
                                   uint32_t sample_count);

/* ========================================================================== */
/*  Layer 1 — ISR hooks (called from audio_playback_interrupts.c)             */
/* ========================================================================== */

/* DAC HT callback: first half of the ring has finished playing, refill it. */
void audio_dma_half_transfer_isr(void);

/* DAC TC callback: second half has finished. Refill it, OR if the source
 * is exhausted, stop DMA and transition to AUDIO_PLAYBACK_DONE.            */
void audio_dma_transfer_complete_isr(void);

/* DMA error callback (overrun / FIFO error). Records to underrun counter
 * and stops cleanly so downstream logic doesn't get stuck.                 */
void audio_dma_error_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYBACK_H */
