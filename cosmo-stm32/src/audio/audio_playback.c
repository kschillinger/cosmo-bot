/*
 * audio_playback.c — implementation of the DAC-driven audio output stack.
 *
 * See audio_playback.h for the architecture overview. Top-of-file comments
 * for each layer explain what's going on; per-function notes are kept short
 * and focused on the non-obvious bits.
 */

#include "audio_playback.h"

#include "stm32l4xx_hal.h"
#include <stdlib.h>     /* abs */
#include <string.h>     /* memset */

/* ========================================================================== */
/*  HAL handles                                                               */
/* ========================================================================== */
/*  These are the same names CubeMX emits if you tick "DAC1 / OUT1, TIM6,
 *  DMA2 stream 4 (DAC1_CH1)".  Defined here as non-static because the HAL
 *  IRQ handlers in audio_playback_interrupts.c need them.                  */

DAC_HandleTypeDef hdac1;
TIM_HandleTypeDef htim6;
DMA_HandleTypeDef hdma_dac1_ch1;

/* ========================================================================== */
/*  Internal state                                                            */
/* ========================================================================== */

typedef struct {
    /* Source buffer (caller-owned) */
    const audio_sample_t *src;
    uint32_t              src_size;       /* total samples in source         */
    uint32_t              src_cursor;     /* next sample to feed the DAC     */

    /* Playback bookkeeping */
    audio_playback_state_t state;
    uint32_t               start_tick_ms;
    uint32_t               duration_ms;

    /* Volume / mute */
    uint8_t  volume_level;     /* 0..100, what we report back               */
    float    volume_gain;      /* level/100, baked in during refill         */
    uint8_t  muted;            /* gain forced to 0 while set                */
    uint8_t  pre_mute_volume;  /* restored by audio_unmute()                */

    /* Amp */
    uint8_t  amp_enabled;

    /* Diagnostics */
    uint32_t dma_underruns;
    uint8_t  initialised;

    /* End-of-clip latch — when the source is fully consumed we let the
     * already-queued half play out, then stop on the next ISR.             */
    uint8_t  source_drained;
} audio_ctx_t;

static audio_ctx_t s_ctx;

/* The single 16-bit DAC ring. Aligned for 16-bit DMA on Cortex-M4.         */
static uint16_t s_dac_ring[AUDIO_DMA_RING_SAMPLES] __attribute__((aligned(4)));

/* ========================================================================== */
/*  Forward declarations                                                      */
/* ========================================================================== */

static void     audio_hal_init(void);
static void     audio_hal_dac_init(void);
static void     audio_hal_tim6_init(void);
static void     audio_hal_dma_init(void);
static void     audio_hal_gpio_init(void);
static void     audio_hal_amplifier_shutdown(uint8_t shutdown);

static void     audio_fill_half(uint16_t *half, uint32_t half_samples);
static uint32_t audio_get_dma_remaining(void);

/* ========================================================================== */
/*  Layer 2 — Buffer prep                                                     */
/* ========================================================================== */

void audio_convert_16bit_to_12bit(const audio_sample_t *in,
                                   uint32_t sample_count,
                                   uint16_t *out)
{
    if (in == NULL || out == NULL) return;

    /* (sample + 32768) >> 4  shifts -32768..+32767 into 0..4095.
     * Doing it as int32_t avoids the silent UB you'd get adding 32768
     * to int16_t.                                                          */
    for (uint32_t i = 0; i < sample_count; ++i) {
        int32_t shifted = (int32_t)in[i] + 32768;
        uint32_t v = (uint32_t)shifted >> 4;
        if (v > AUDIO_DAC_MAX_VALUE) v = AUDIO_DAC_MAX_VALUE; /* defensive */
        out[i] = (uint16_t)v;
    }
}

void audio_apply_gain(audio_sample_t *samples,
                      uint32_t sample_count,
                      float gain)
{
    if (samples == NULL) return;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > AUDIO_MAX_GAIN) gain = AUDIO_MAX_GAIN;

    /* Branchless saturation into int16_t */
    for (uint32_t i = 0; i < sample_count; ++i) {
        int32_t v = (int32_t)((float)samples[i] * gain);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        samples[i] = (int16_t)v;
    }
}

uint16_t audio_compute_peak_level(const audio_sample_t *samples,
                                   uint32_t sample_count)
{
    if (samples == NULL) return 0;
    uint16_t peak = 0;
    for (uint32_t i = 0; i < sample_count; ++i) {
        int32_t a = samples[i];
        if (a < 0) a = -a;
        if ((uint16_t)a > peak) peak = (uint16_t)a;
    }
    return peak;
}

/* ========================================================================== */
/*  Layer 1 — Hardware abstraction                                            */
/* ========================================================================== */

/*  Clock plan (16 kHz sample rate from 80 MHz APB1 timer clock):
 *    PSC = 0, ARR = 4999  →  80 MHz / 5000 = 16 000 Hz exactly.
 *  TIM6 generates TRGO on each update event, which the DAC takes as its
 *  conversion trigger. No CPU involvement per sample.                      */

static void audio_hal_tim6_init(void)
{
    __HAL_RCC_TIM6_CLK_ENABLE();

    htim6.Instance               = TIM6;
    htim6.Init.Prescaler         = 0U;
    htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim6.Init.Period            = (SystemCoreClock / AUDIO_SAMPLE_RATE_HZ) - 1U;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim6) != HAL_OK) {
        /* Hard fault for now — hooked up to a CubeMX-style error handler
         * if you have one in main.c.                                       */
        while (1) {}
    }

    TIM_MasterConfigTypeDef master = {0};
    master.MasterOutputTrigger = TIM_TRGO_UPDATE;
    master.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &master) != HAL_OK) {
        while (1) {}
    }
}

/*  DMA2 Channel 4, request 3 = DAC1_CH1 on STM32L476.
 *  (Spec mentioned DMA2 Ch3, but that request line doesn't map to the DAC
 *  on this part — the screendrivers branch already owns DMA1 Ch3 for SPI1
 *  TX, so we route DAC through DMA2 to avoid sharing.)
 *
 *  Circular mode + half-transfer interrupt = ping-pong refill.            */

static void audio_hal_dma_init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_dac1_ch1.Instance                 = DMA2_Channel4;
    hdma_dac1_ch1.Init.Request             = DMA_REQUEST_3;
    hdma_dac1_ch1.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_dac1_ch1.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_dac1_ch1.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_dac1_ch1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_dac1_ch1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_dac1_ch1.Init.Mode                = DMA_CIRCULAR;
    hdma_dac1_ch1.Init.Priority            = DMA_PRIORITY_HIGH;

    if (HAL_DMA_Init(&hdma_dac1_ch1) != HAL_OK) {
        while (1) {}
    }
    __HAL_LINKDMA(&hdac1, DMA_Handle1, hdma_dac1_ch1);

    HAL_NVIC_SetPriority(DMA2_Channel4_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Channel4_IRQn);
}

static void audio_hal_dac_init(void)
{
    __HAL_RCC_DAC1_CLK_ENABLE();

    hdac1.Instance = DAC1;
    if (HAL_DAC_Init(&hdac1) != HAL_OK) {
        while (1) {}
    }

    DAC_ChannelConfTypeDef ch = {0};
    ch.DAC_SampleAndHold      = DAC_SAMPLEANDHOLD_DISABLE;
    ch.DAC_Trigger            = DAC_TRIGGER_T6_TRGO;
    ch.DAC_OutputBuffer       = DAC_OUTPUTBUFFER_ENABLE;  /* low-Z drive */
    ch.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
    ch.DAC_UserTrimming       = DAC_TRIMMING_FACTORY;

    if (HAL_DAC_ConfigChannel(&hdac1, &ch, DAC_CHANNEL_1) != HAL_OK) {
        while (1) {}
    }
}

static void audio_hal_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PA4 — DAC1_OUT1, analog mode, no pull. */
    GPIO_InitTypeDef pa4 = {0};
    pa4.Pin  = GPIO_PIN_4;
    pa4.Mode = GPIO_MODE_ANALOG;
    pa4.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &pa4);

    /* PB2 — amplifier SHUTDOWN.  Drive LOW first so we don't wake the amp
     * mid-init and pop the speaker.                                       */
    HAL_GPIO_WritePin(AUDIO_AMP_SHUTDOWN_PORT, AUDIO_AMP_SHUTDOWN_PIN,
                      GPIO_PIN_RESET);

    GPIO_InitTypeDef pb2 = {0};
    pb2.Pin   = AUDIO_AMP_SHUTDOWN_PIN;
    pb2.Mode  = GPIO_MODE_OUTPUT_PP;
    pb2.Pull  = GPIO_NOPULL;
    pb2.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(AUDIO_AMP_SHUTDOWN_PORT, &pb2);
}

static void audio_hal_init(void)
{
    audio_hal_gpio_init();
    audio_hal_tim6_init();
    audio_hal_dma_init();
    audio_hal_dac_init();
}

static void audio_hal_amplifier_shutdown(uint8_t shutdown)
{
    /* PAM8302 SD: HIGH = run, LOW = shutdown. */
    HAL_GPIO_WritePin(AUDIO_AMP_SHUTDOWN_PORT,
                      AUDIO_AMP_SHUTDOWN_PIN,
                      shutdown ? GPIO_PIN_RESET : GPIO_PIN_SET);
    s_ctx.amp_enabled = shutdown ? 0u : 1u;
}

/* Returns the number of DAC ring slots the DMA hasn't transferred yet.
 * Used both by the progress estimator and by the ISRs that need to know
 * which half just finished.                                                */
static uint32_t audio_get_dma_remaining(void)
{
    return __HAL_DMA_GET_COUNTER(&hdma_dac1_ch1);
}

/* ========================================================================== */
/*  Ping-pong refill                                                          */
/* ========================================================================== */
/*  Called from the half-transfer / transfer-complete ISRs. Each call gets
 *  AUDIO_DMA_HALF_SAMPLES worth of DAC output to fill. We pull from
 *  s_ctx.src, apply gain, convert, advance the cursor. If the source is
 *  exhausted partway through, the rest of the half is filled with mid-scale
 *  silence and we latch source_drained so the *next* ISR can stop the DMA
 *  cleanly (after the silence has actually played).                       */

static void audio_fill_half(uint16_t *half, uint32_t half_samples)
{
    /* If we already drained the source and silence has played, stop. */
    if (s_ctx.source_drained) {
        /* Stop happens in the ISR wrapper after this returns; here we
         * just keep emitting silence so DMA never reads stale junk.   */
        for (uint32_t i = 0; i < half_samples; ++i) {
            half[i] = (uint16_t)AUDIO_DAC_MID_SCALE;
        }
        return;
    }

    const float    gain  = s_ctx.muted ? 0.0f : s_ctx.volume_gain;
    const uint32_t left  = (s_ctx.src_size > s_ctx.src_cursor)
                           ? (s_ctx.src_size - s_ctx.src_cursor)
                           : 0U;
    const uint32_t take  = (left < half_samples) ? left : half_samples;

    /* Real samples: gain + 16->12 conversion in one pass. */
    for (uint32_t i = 0; i < take; ++i) {
        int32_t s = (int32_t)((float)s_ctx.src[s_ctx.src_cursor + i] * gain);
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        uint32_t v = (uint32_t)(s + 32768) >> 4;
        if (v > AUDIO_DAC_MAX_VALUE) v = AUDIO_DAC_MAX_VALUE;
        half[i] = (uint16_t)v;
    }
    s_ctx.src_cursor += take;

    /* Tail: pad with silence so the amp sees a clean DC level at end-of-clip. */
    if (take < half_samples) {
        for (uint32_t i = take; i < half_samples; ++i) {
            half[i] = (uint16_t)AUDIO_DAC_MID_SCALE;
        }
        s_ctx.source_drained = 1u;
    }
}

/* ========================================================================== */
/*  ISR hooks                                                                 */
/* ========================================================================== */

void audio_dma_half_transfer_isr(void)
{
    /* DMA just crossed the midpoint — the FIRST half is no longer being
     * read by the DAC, so it's safe to overwrite it.                      */
    audio_fill_half(&s_dac_ring[0], AUDIO_DMA_HALF_SAMPLES);
}

void audio_dma_transfer_complete_isr(void)
{
    /* DMA wrapped around — the SECOND half just finished playing. */

    if (s_ctx.source_drained) {
        /* We already pushed the trailing silence on the previous ISR;
         * the DAC has now actually emitted it, so we can stop without
         * an audible click.                                              */
        HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
        HAL_TIM_Base_Stop(&htim6);
        s_ctx.state = AUDIO_PLAYBACK_DONE;
        return;
    }
    audio_fill_half(&s_dac_ring[AUDIO_DMA_HALF_SAMPLES],
                    AUDIO_DMA_HALF_SAMPLES);
}

void audio_dma_error_isr(void)
{
    s_ctx.dma_underruns++;
    HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
    HAL_TIM_Base_Stop(&htim6);
    s_ctx.state = AUDIO_PLAYBACK_IDLE;
}

/* ========================================================================== */
/*  Layer 3 — Playback control                                                */
/* ========================================================================== */

void audio_init(void)
{
    if (s_ctx.initialised) {
        return;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.state           = AUDIO_PLAYBACK_IDLE;
    s_ctx.volume_level    = AUDIO_DEFAULT_VOLUME;
    s_ctx.volume_gain     = (float)AUDIO_DEFAULT_VOLUME / 100.0f;
    s_ctx.pre_mute_volume = AUDIO_DEFAULT_VOLUME;

    /* Pre-fill the ring with silence so the very first DMA cycles don't
     * spit garbage into the amp.                                         */
    for (uint32_t i = 0; i < AUDIO_DMA_RING_SAMPLES; ++i) {
        s_dac_ring[i] = (uint16_t)AUDIO_DAC_MID_SCALE;
    }

    audio_hal_init();
    audio_hal_amplifier_shutdown(0);  /* amp on, idling at DAC mid-scale */
    s_ctx.initialised = 1u;
}

audio_status_t audio_play_buffer(const audio_sample_t *buffer,
                                  uint32_t size_samples)
{
    if (!s_ctx.initialised)        return AUDIO_ERR_NOT_INITIALISED;
    if (buffer == NULL)            return AUDIO_ERR_NULL_BUFFER;
    if (size_samples == 0U)        return AUDIO_ERR_EMPTY_BUFFER;

    /* If something is already playing, wind it down first. The TC ISR
     * will not fire spuriously after this because we stop both DAC and
     * timer.                                                            */
    if (s_ctx.state == AUDIO_PLAYBACK_RUNNING) {
        HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
        HAL_TIM_Base_Stop(&htim6);
    }

    /* Snapshot the new clip into the context. */
    s_ctx.src             = buffer;
    s_ctx.src_size        = size_samples;
    s_ctx.src_cursor      = 0U;
    s_ctx.source_drained  = 0u;
    s_ctx.start_tick_ms   = HAL_GetTick();
    s_ctx.duration_ms     = (size_samples * 1000U) / AUDIO_SAMPLE_RATE_HZ;

    /* Pre-fill BOTH halves before we kick the DMA, so the first samples
     * are real audio instead of leftover silence from the previous run. */
    audio_fill_half(&s_dac_ring[0],
                    AUDIO_DMA_HALF_SAMPLES);
    audio_fill_half(&s_dac_ring[AUDIO_DMA_HALF_SAMPLES],
                    AUDIO_DMA_HALF_SAMPLES);

    /* Make sure the amp is awake. */
    if (!s_ctx.amp_enabled) {
        audio_hal_amplifier_shutdown(0);
    }

    /* Kick it off. HAL_DAC_Start_DMA wires up the half-transfer +
     * transfer-complete + error interrupts to the HAL_DAC_Conv*_Cb
     * callbacks, which we route in audio_playback_interrupts.c.        */
    if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1,
                          (uint32_t *)s_dac_ring,
                          AUDIO_DMA_RING_SAMPLES,
                          DAC_ALIGN_12B_R) != HAL_OK) {
        s_ctx.state = AUDIO_PLAYBACK_IDLE;
        return AUDIO_ERR_HARDWARE;
    }
    if (HAL_TIM_Base_Start(&htim6) != HAL_OK) {
        HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
        s_ctx.state = AUDIO_PLAYBACK_IDLE;
        return AUDIO_ERR_HARDWARE;
    }

    s_ctx.state = AUDIO_PLAYBACK_RUNNING;
    return AUDIO_OK;
}

void audio_playback_stop(void)
{
    if (!s_ctx.initialised) return;

    HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
    HAL_TIM_Base_Stop(&htim6);

    /* Park the ring at silence so the DAC pin sits at mid-scale. */
    for (uint32_t i = 0; i < AUDIO_DMA_RING_SAMPLES; ++i) {
        s_dac_ring[i] = (uint16_t)AUDIO_DAC_MID_SCALE;
    }

    s_ctx.src             = NULL;
    s_ctx.src_size        = 0U;
    s_ctx.src_cursor      = 0U;
    s_ctx.source_drained  = 0u;
    s_ctx.state           = AUDIO_PLAYBACK_IDLE;
}

uint8_t audio_playback_is_done(void)
{
    /* The ISR transitions to DONE when the trailing-silence half finishes;
     * we report that latch and reset to IDLE so a second call doesn't keep
     * returning 1 forever.                                                 */
    if (s_ctx.state == AUDIO_PLAYBACK_DONE) {
        s_ctx.state = AUDIO_PLAYBACK_IDLE;
        return 1u;
    }
    return 0u;
}

uint32_t audio_playback_get_progress_percent(void)
{
    if (s_ctx.state != AUDIO_PLAYBACK_RUNNING || s_ctx.src_size == 0U) {
        return 0U;
    }
    /* src_cursor is "samples already pushed into the ring", not "samples
     * already heard", but the lag is at most one ring (64 ms) so for a
     * progress UI it's plenty accurate.                                  */
    uint64_t pct = ((uint64_t)s_ctx.src_cursor * 100ULL) / s_ctx.src_size;
    if (pct > 100ULL) pct = 100ULL;
    return (uint32_t)pct;
}

audio_playback_state_t audio_playback_get_state(void)
{
    return s_ctx.state;
}

/* ========================================================================== */
/*  Layer 4 — Volume control                                                  */
/* ========================================================================== */

void audio_set_volume(uint8_t volume_percent)
{
    if (volume_percent > 100U) volume_percent = 100U;
    s_ctx.volume_level = volume_percent;
    s_ctx.volume_gain  = (float)volume_percent / 100.0f;
    /* Volume is applied at refill time; no buffer rewrite needed. */
}

uint8_t audio_get_volume(void)
{
    return s_ctx.volume_level;
}

void audio_mute(void)
{
    if (s_ctx.muted) return;
    s_ctx.pre_mute_volume = s_ctx.volume_level;
    s_ctx.muted           = 1u;
    /* gain is sampled per-refill via the muted flag */
}

void audio_unmute(void)
{
    if (!s_ctx.muted) return;
    s_ctx.muted = 0u;
    audio_set_volume(s_ctx.pre_mute_volume);
}

uint8_t audio_is_muted(void)
{
    return s_ctx.muted;
}

void audio_amplifier_shutdown(uint8_t shutdown)
{
    audio_hal_amplifier_shutdown(shutdown);
}
