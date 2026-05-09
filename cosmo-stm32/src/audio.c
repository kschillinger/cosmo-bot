/**
 * @file    audio.c
 * @brief   PDM microphone capture pipeline — implementation.
 *
 * See audio.h for architecture overview and call patterns.
 *
 * Threading model:
 *   - DMA half-/full-transfer ISR    : updates write_index, sample_count
 *   - TIM6 ISR (10 Hz)               : RMS, silence detect, waveform extract
 *   - Main loop / FSM                : start/stop, read buffer for STT
 *
 * Concurrency strategy:
 *   ISRs only *write* shared state (volatile fields). The main loop reads
 *   them. No critical sections needed for single-word loads/stores on
 *   Cortex-M4 — it's all 32-bit aligned and atomic.
 */

#include "audio.h"

#include <string.h>   /* memset                                             */
#include <stdlib.h>   /* abs                                                */

#include "stm32l4xx_hal.h"

/* ========================================================================== */
/*  HAL handles — defined in main.c by CubeMX                                 */
/* ========================================================================== */

extern SAI_HandleTypeDef hsai_BlockA1;   /**< PDM mic input via SAI1_A     */
extern TIM_HandleTypeDef htim6;          /**< 10 Hz waveform / detect tick */

/** Convenience alias so the rest of the file can ignore the block name. */
#define AUDIO_SAI_HANDLE  (&hsai_BlockA1)
#define AUDIO_TIM_HANDLE  (&htim6)

/* ========================================================================== */
/*  Module globals                                                            */
/* ========================================================================== */

/* The capture buffer is large (64 KiB) and DMA-targeted — give it 4-byte
 * alignment so the SAI peripheral is happy on every wrap. */
__attribute__((aligned(4)))
audio_buffer_t        audio_buf;

waveform_buffer_t     waveform;

audio_capture_state_t capture_state = {
    .state                   = AUDIO_STATE_IDLE,
    .recording_start_time_ms = 0,
    .recording_duration_ms   = 0,
    .silence_duration_ms     = 0,
    .sound_detected          = 0,
    .audio_level_rms         = 0,
    .audio_level_peak        = 0,
    .silence_threshold       = AUDIO_DEFAULT_SILENCE_THRESHOLD,
    .auto_stop_timeout_ms    = AUDIO_DEFAULT_AUTOSTOP_MS,
};

/* ========================================================================== */
/*  Layer 1 — HAL abstraction                                                 */
/* ========================================================================== */

/**
 * Hardware init. The peripheral instances themselves (SAI1_A, DMA2_Ch6,
 * TIM6) are configured by CubeMX-generated code in MX_*_Init() — this
 * function only takes ownership of them and starts the timer.
 */
void audio_hal_init(void)
{
    /* Make sure capture is idle until the FSM explicitly asks for it. */
    audio_hal_stop_dma();

    /* TIM6 ticks at WAVEFORM_UPDATE_RATE_HZ; its update interrupt drives
     * audio_update_sound_detection() + audio_extract_waveform(). */
    HAL_TIM_Base_Start_IT(AUDIO_TIM_HANDLE);
}

/**
 * Kick off circular DMA from SAI into audio_buf.buffer. Half-transfer and
 * transfer-complete interrupts will fire each ~1 s for a 32k buffer at
 * 16 kHz, but the ISR uses NDTR to compute exact sample positions.
 */
void audio_hal_start_dma(void)
{
    /* HAL_SAI_Receive_DMA expects a byte count for 8-bit data, halfword
     * count for 16-bit. Pass AUDIO_BUFFER_SIZE samples. */
    HAL_SAI_Receive_DMA(AUDIO_SAI_HANDLE,
                        (uint8_t *)audio_buf.buffer,
                        AUDIO_BUFFER_SIZE);
}

void audio_hal_stop_dma(void)
{
    HAL_SAI_DMAStop(AUDIO_SAI_HANDLE);
}

/**
 * Translate the DMA's "remaining transfers" register into a write index.
 * The DMA stream is configured CIRCULAR, so NDTR counts down from
 * AUDIO_BUFFER_SIZE to 0 and reloads.
 */
void audio_hal_get_dma_write_index(uint32_t *index)
{
    if (!index) {
        return;
    }
    DMA_HandleTypeDef *hdma = AUDIO_SAI_HANDLE->hdmarx;
    if (!hdma) {
        *index = 0;
        return;
    }
    uint32_t remaining = __HAL_DMA_GET_COUNTER(hdma);
    /* Guard against transient remaining > buffer size during reload. */
    if (remaining > AUDIO_BUFFER_SIZE) {
        remaining = AUDIO_BUFFER_SIZE;
    }
    *index = AUDIO_BUFFER_SIZE - remaining;
}

/* ========================================================================== */
/*  Layer 2 — Circular buffer management                                      */
/* ========================================================================== */

void audio_buffer_init(void)
{
    memset(audio_buf.buffer, 0, AUDIO_BUFFER_SIZE_BYTES);
    audio_buf.write_index  = 0;
    audio_buf.read_index   = 0;
    audio_buf.sample_count = 0;
    audio_buf.overflow     = 0;
}

void audio_buffer_reset(void)
{
    /* Same as init for now, but kept separate so a future caller can reset
     * indices without paying for the 64 KB memset. */
    audio_buf.write_index  = 0;
    audio_buf.read_index   = 0;
    audio_buf.sample_count = 0;
    audio_buf.overflow     = 0;
    memset(audio_buf.buffer, 0, AUDIO_BUFFER_SIZE_BYTES);
}

uint32_t audio_buffer_get_write_index(void)
{
    return audio_buf.write_index;
}

/**
 * Update bookkeeping after the DMA has advanced. Detects the wrap-around
 * that means the producer overtook the consumer, and rolls the absolute
 * sample_count counter forward.
 */
void audio_buffer_set_write_index(uint32_t index)
{
    uint32_t prev = audio_buf.write_index;
    uint32_t now  = index % AUDIO_BUFFER_SIZE;

    /* Did we wrap since the last call? Then we lost data. */
    if (now < prev) {
        audio_buf.overflow = 1;
        audio_buf.sample_count += (AUDIO_BUFFER_SIZE - prev) + now;
    } else {
        audio_buf.sample_count += (now - prev);
    }
    audio_buf.write_index = now;
}

uint32_t audio_buffer_get_sample_count(void)
{
    return audio_buf.sample_count;
}

audio_sample_t *audio_buffer_get_ptr(uint32_t offset)
{
    if (offset >= AUDIO_BUFFER_SIZE) {
        return NULL;
    }
    return &audio_buf.buffer[offset];
}

/* ========================================================================== */
/*  Layer 3 — Sound detection                                                 */
/* ========================================================================== */

/**
 * Integer square root via Newton-Raphson — ~6 iterations, no FPU needed
 * for this path (keeps the FPU free for other DSP if added later).
 */
static uint32_t isqrt_u32(uint32_t n)
{
    if (n == 0) {
        return 0;
    }
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

uint16_t audio_compute_rms(uint32_t start_index, uint32_t sample_count)
{
    if (sample_count == 0) {
        return 0;
    }
    /* sum_sq fits in 64 bits worst-case: 32767² × 32000 ≈ 3.4e13 < 2^45. */
    uint64_t sum_sq = 0;
    for (uint32_t i = 0; i < sample_count; ++i) {
        uint32_t idx = (start_index + i) % AUDIO_BUFFER_SIZE;
        int32_t  s   = (int32_t)audio_buf.buffer[idx];
        sum_sq += (uint64_t)(s * s);
    }
    uint32_t mean_sq = (uint32_t)(sum_sq / sample_count);
    uint32_t rms     = isqrt_u32(mean_sq);
    if (rms > 0xFFFFU) {
        rms = 0xFFFFU;
    }
    return (uint16_t)rms;
}

uint16_t audio_compute_peak(uint32_t start_index, uint32_t sample_count)
{
    uint32_t peak = 0;
    for (uint32_t i = 0; i < sample_count; ++i) {
        uint32_t idx = (start_index + i) % AUDIO_BUFFER_SIZE;
        int32_t  s   = (int32_t)audio_buf.buffer[idx];
        uint32_t a   = (s < 0) ? (uint32_t)(-s) : (uint32_t)s;
        if (a > peak) {
            peak = a;
        }
    }
    if (peak > 0xFFFFU) {
        peak = 0xFFFFU;
    }
    return (uint16_t)peak;
}

uint8_t audio_is_silent(uint16_t audio_level, uint16_t threshold)
{
    return (audio_level < threshold) ? 1 : 0;
}

/**
 * Called once per waveform tick (~100 ms). Computes RMS over the most
 * recent window of audio, decides whether we're listening to silence or
 * speech, and trips auto-stop when silence has lasted long enough.
 */
void audio_update_sound_detection(void)
{
    if (capture_state.state != AUDIO_STATE_RECORDING) {
        return;
    }

    uint32_t total = audio_buffer_get_sample_count();
    uint32_t window = (total < AUDIO_RMS_WINDOW_SAMPLES)
                      ? total
                      : AUDIO_RMS_WINDOW_SAMPLES;
    if (window == 0) {
        return;
    }

    /* The newest `window` samples end at write_index. */
    uint32_t w     = audio_buffer_get_write_index();
    uint32_t start = (w + AUDIO_BUFFER_SIZE - window) % AUDIO_BUFFER_SIZE;

    uint16_t rms  = audio_compute_rms (start, window);
    uint16_t peak = audio_compute_peak(start, window);

    capture_state.audio_level_rms  = rms;
    capture_state.audio_level_peak = peak;

    if (audio_is_silent(rms, capture_state.silence_threshold)) {
        capture_state.sound_detected      = 0;
        capture_state.silence_duration_ms += WAVEFORM_UPDATE_PERIOD_MS;
    } else {
        capture_state.sound_detected      = 1;
        capture_state.silence_duration_ms = 0;
    }
}

void audio_set_silence_threshold(uint16_t threshold)
{
    capture_state.silence_threshold = threshold;
}

/* ========================================================================== */
/*  Layer 4 — Waveform extraction                                             */
/* ========================================================================== */

/**
 * Downsample the captured audio into WAVEFORM_SAMPLE_COUNT bins by taking
 * the peak absolute amplitude in each bin and normalising to 0..65535.
 * Cheap, visually pleasing, and correct even when the buffer hasn't filled.
 */
void audio_extract_waveform(void)
{
    uint32_t now = system_get_tick_ms();
    if ((now - waveform.last_update_time_ms) < WAVEFORM_UPDATE_PERIOD_MS &&
        waveform.valid) {
        return;
    }

    uint32_t total = audio_buffer_get_sample_count();
    if (total == 0) {
        memset(waveform.samples, 0, sizeof(waveform.samples));
        waveform.valid               = 1;
        waveform.last_update_time_ms = now;
        return;
    }

    /* Walk back from write_index over the last min(total, BUFFER_SIZE)
     * samples — that's the pool we downsample. */
    uint32_t span  = (total < AUDIO_BUFFER_SIZE) ? total : AUDIO_BUFFER_SIZE;
    uint32_t w     = audio_buffer_get_write_index();
    uint32_t start = (w + AUDIO_BUFFER_SIZE - span) % AUDIO_BUFFER_SIZE;

    /* At least one sample per bin; use ceiling division. */
    uint32_t per_bin = (span + WAVEFORM_SAMPLE_COUNT - 1) / WAVEFORM_SAMPLE_COUNT;
    if (per_bin == 0) {
        per_bin = 1;
    }

    for (uint32_t bin = 0; bin < WAVEFORM_SAMPLE_COUNT; ++bin) {
        uint32_t bin_start = bin * per_bin;
        if (bin_start >= span) {
            waveform.samples[bin] = 0;
            continue;
        }
        uint32_t bin_len = per_bin;
        if (bin_start + bin_len > span) {
            bin_len = span - bin_start;
        }
        uint32_t idx  = (start + bin_start) % AUDIO_BUFFER_SIZE;
        uint16_t peak = audio_compute_peak(idx, bin_len);
        /* Normalise int16 peak (0..32767) → uint16 (0..65535). */
        uint32_t scaled = (uint32_t)peak * 2U;
        if (scaled > 0xFFFFU) {
            scaled = 0xFFFFU;
        }
        waveform.samples[bin] = (uint16_t)scaled;
    }

    waveform.valid               = 1;
    waveform.last_update_time_ms = now;
}

uint16_t *audio_get_waveform_samples(void)     { return waveform.samples; }
uint16_t  audio_get_waveform_sample_count(void){ return WAVEFORM_SAMPLE_COUNT; }
uint8_t   audio_is_waveform_valid(void)        { return waveform.valid; }

/* ========================================================================== */
/*  Layer 5 — Public API consumed by the FSM                                  */
/* ========================================================================== */

void audio_init(void)
{
    audio_buffer_init();
    memset(&waveform, 0, sizeof(waveform));
    capture_state.state                   = AUDIO_STATE_IDLE;
    capture_state.recording_start_time_ms = 0;
    capture_state.recording_duration_ms   = 0;
    capture_state.silence_duration_ms     = 0;
    capture_state.sound_detected          = 0;
    capture_state.audio_level_rms         = 0;
    capture_state.audio_level_peak        = 0;
    capture_state.silence_threshold       = AUDIO_DEFAULT_SILENCE_THRESHOLD;
    capture_state.auto_stop_timeout_ms    = AUDIO_DEFAULT_AUTOSTOP_MS;
    audio_hal_init();
}

void audio_capture_start(void)
{
    if (capture_state.state == AUDIO_STATE_RECORDING) {
        return;                          /* idempotent */
    }
    audio_buffer_reset();
    waveform.valid = 0;

    capture_state.state                   = AUDIO_STATE_RECORDING;
    capture_state.recording_start_time_ms = system_get_tick_ms();
    capture_state.recording_duration_ms   = 0;
    capture_state.silence_duration_ms     = 0;
    capture_state.sound_detected          = 0;
    capture_state.audio_level_rms         = 0;
    capture_state.audio_level_peak        = 0;

    audio_hal_start_dma();
}

void audio_capture_stop(void)
{
    if (capture_state.state != AUDIO_STATE_RECORDING) {
        return;
    }
    audio_hal_stop_dma();
    capture_state.recording_duration_ms =
        system_get_tick_ms() - capture_state.recording_start_time_ms;
    capture_state.state = AUDIO_STATE_DONE;
}

uint8_t audio_capture_is_active(void)
{
    return (capture_state.state == AUDIO_STATE_RECORDING) ? 1 : 0;
}

/**
 * Returns 1 when the FSM should leave LISTENING. Two conditions:
 *   - silence has lasted >= auto_stop_timeout_ms
 *   - max recording duration reached (safety; prevents buffer wrap)
 */
uint8_t audio_capture_is_auto_stop_triggered(void)
{
    if (capture_state.state != AUDIO_STATE_RECORDING) {
        return 0;
    }
    uint32_t elapsed =
        system_get_tick_ms() - capture_state.recording_start_time_ms;

    if (capture_state.silence_duration_ms >= capture_state.auto_stop_timeout_ms) {
        return 1;                        /* user stopped speaking */
    }
    if (elapsed >= AUDIO_CAPTURE_DURATION_MAX_S * 1000U) {
        return 1;                        /* hard cap (buffer full) */
    }
    return 0;
}

audio_sample_t *audio_get_buffer(void)        { return audio_buf.buffer; }

uint32_t audio_get_buffer_size(void)
{
    uint32_t n = audio_buf.sample_count;
    if (n > AUDIO_BUFFER_SIZE) {
        n = AUDIO_BUFFER_SIZE;
    }
    return n * sizeof(audio_sample_t);
}

uint16_t *audio_get_samples(void)             { return waveform.samples; }
uint16_t  audio_get_sample_count(void)        { return WAVEFORM_SAMPLE_COUNT; }
uint8_t   audio_is_sound_detected(void)       { return capture_state.sound_detected; }
uint16_t  audio_get_level_rms(void)           { return capture_state.audio_level_rms; }
uint16_t  audio_get_level_peak(void)          { return capture_state.audio_level_peak; }

uint32_t audio_get_recording_duration_ms(void)
{
    if (capture_state.state == AUDIO_STATE_RECORDING) {
        return system_get_tick_ms() - capture_state.recording_start_time_ms;
    }
    return capture_state.recording_duration_ms;
}
