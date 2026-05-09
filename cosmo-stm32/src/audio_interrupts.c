/**
 * @file    audio_interrupts.c
 * @brief   Interrupt-context glue between SAI/DMA + TIM6 and the audio driver.
 *
 * What runs where:
 *
 *   ┌─────────────────────────┐  half/full xfer  ┌──────────────────────┐
 *   │  SAI1 DMA (DMA2_Ch6)    │ ───────────────▶ │ HAL_SAI_RxHalfCplt /  │
 *   │  PDM mic → audio_buf    │                  │ HAL_SAI_RxCpltCallback │
 *   └─────────────────────────┘                  └──────────┬───────────┘
 *                                                           │
 *                                                           ▼
 *                                          audio_buffer_set_write_index()
 *
 *   ┌─────────────────────────┐    update IRQ    ┌──────────────────────┐
 *   │  TIM6 (10 Hz)           │ ───────────────▶ │ HAL_TIM_PeriodElapsed │
 *   └─────────────────────────┘                  └──────────┬───────────┘
 *                                                           │
 *                                                           ▼
 *                                       audio_update_sound_detection()
 *                                       audio_extract_waveform()
 *
 *   ┌─────────────────────────┐   EXTI rising    ┌──────────────────────┐
 *   │  USER button (PC13)     │ ───────────────▶ │ HAL_GPIO_EXTI_Callback│
 *   └─────────────────────────┘                  └──────────┬───────────┘
 *                                                           │
 *                                                           ▼
 *                                          toggle audio_capture_start/stop
 *
 * The CubeMX-generated weak ISR shells (DMA2_Channel6_IRQHandler,
 * TIM6_DAC_IRQHandler, EXTI15_10_IRQHandler) live in stm32l4xx_it.c and
 * already call HAL_DMA_IRQHandler / HAL_TIM_IRQHandler / HAL_GPIO_EXTI_IRQHandler.
 * Those route to the callbacks defined below, so nothing here touches NVIC
 * registers directly.
 */

#include "audio.h"

#include "stm32l4xx_hal.h"

/* ========================================================================== */
/*  Optional GPIO indicator (LD2 on Nucleo-L476RG = PA5)                      */
/* ========================================================================== */

#ifndef AUDIO_REC_LED_PORT
#define AUDIO_REC_LED_PORT  GPIOA
#endif
#ifndef AUDIO_REC_LED_PIN
#define AUDIO_REC_LED_PIN   GPIO_PIN_5
#endif

/* ========================================================================== */
/*  Optional manual-trigger button (B1 on Nucleo-L476RG = PC13)               */
/* ========================================================================== */

#ifndef AUDIO_BUTTON_PIN
#define AUDIO_BUTTON_PIN    GPIO_PIN_13
#endif

#define AUDIO_BUTTON_DEBOUNCE_MS  50U

extern SAI_HandleTypeDef hsai_BlockA1;
extern TIM_HandleTypeDef htim6;

/* ========================================================================== */
/*  SAI / DMA callbacks                                                       */
/* ========================================================================== */

/**
 * Half-transfer: DMA has filled the lower half of audio_buf.buffer.
 * We don't actually care which half — we just re-read NDTR to learn
 * the precise write pointer and let the buffer module update its state.
 */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai != &hsai_BlockA1) {
        return;
    }
    uint32_t pos = 0;
    audio_hal_get_dma_write_index(&pos);
    audio_buffer_set_write_index(pos);
}

/**
 * Full transfer: the DMA has just wrapped to offset 0. Same handling as
 * the half-transfer case — the buffer module computes the wrap from the
 * old/new write_index relationship.
 */
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai != &hsai_BlockA1) {
        return;
    }
    uint32_t pos = 0;
    audio_hal_get_dma_write_index(&pos);
    audio_buffer_set_write_index(pos);
}

/**
 * SAI / DMA error. Stop capture and let the FSM observe the dropped state
 * via audio_capture_is_active() == 0.
 */
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai != &hsai_BlockA1) {
        return;
    }
    audio_capture_stop();
    /* Mark the buffer overflow flag so downstream code knows the capture
     * is suspect. */
    extern audio_buffer_t audio_buf;
    audio_buf.overflow = 1;
}

/* ========================================================================== */
/*  Timer callback — 10 Hz waveform / silence-detect tick                     */
/* ========================================================================== */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim != &htim6) {
        return;                          /* not our timer */
    }
    /* Refresh write_index from NDTR so RMS / waveform see the freshest data,
     * even between SAI half/full callbacks. */
    uint32_t pos = 0;
    audio_hal_get_dma_write_index(&pos);
    audio_buffer_set_write_index(pos);

    audio_update_sound_detection();
    audio_extract_waveform();
}

/* ========================================================================== */
/*  GPIO EXTI callback — manual record toggle on B1                           */
/* ========================================================================== */

/**
 * Manual override: pressing the user button toggles capture. Useful for
 * bring-up before sound-detection thresholds are tuned.
 *
 * Debounced in software — EXTI ignores presses arriving inside the
 * debounce window.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin != AUDIO_BUTTON_PIN) {
        return;
    }
    static uint32_t last_press_ms = 0;
    uint32_t now = system_get_tick_ms();
    if ((now - last_press_ms) < AUDIO_BUTTON_DEBOUNCE_MS) {
        return;
    }
    last_press_ms = now;

    if (audio_capture_is_active()) {
        audio_capture_stop();
        HAL_GPIO_WritePin(AUDIO_REC_LED_PORT, AUDIO_REC_LED_PIN, GPIO_PIN_RESET);
    } else {
        audio_capture_start();
        HAL_GPIO_WritePin(AUDIO_REC_LED_PORT, AUDIO_REC_LED_PIN, GPIO_PIN_SET);
    }
}
