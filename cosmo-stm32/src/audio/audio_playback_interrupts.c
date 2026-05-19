/*
 * audio_playback_interrupts.c — IRQ glue for the audio playback driver.
 *
 * The DAC's circular DMA fires three callbacks of interest:
 *
 *   HAL_DAC_ConvHalfCpltCallbackCh1   — first half of ring just finished
 *   HAL_DAC_ConvCpltCallbackCh1       — second half just finished (wrap)
 *   HAL_DAC_ErrorCallbackCh1          — DMA error (overrun / FIFO)
 *
 * We forward those into the three audio_dma_*_isr hooks in audio_playback.c,
 * which are where the actual ping-pong refill logic lives.
 *
 * If you have a CubeMX-generated stm32l4xx_it.c in your project, REMOVE its
 * DMA2_Channel4_IRQHandler — or it will conflict with the one defined here.
 * (Conversely, if you'd prefer to keep CubeMX's, just make sure that handler
 * calls HAL_DMA_IRQHandler(&hdma_dac1_ch1) like the one below.)
 */

#include "audio_playback.h"
#include "stm32l4xx_hal.h"

/* Provided by audio_playback.c. */
extern DAC_HandleTypeDef hdac1;
extern DMA_HandleTypeDef hdma_dac1_ch1;

/* ========================================================================== */
/*  Top-level NVIC vector                                                     */
/* ========================================================================== */
/*  This is the only ISR in the audio subsystem that the NVIC actually
 *  vectors to. HAL_DMA_IRQHandler unpacks the DMA status flags, clears
 *  them, and invokes whichever DAC callback is appropriate.                */

void DMA2_Channel4_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_dac1_ch1);
}

/* ========================================================================== */
/*  HAL DAC callbacks                                                          */
/* ========================================================================== */
/*  These names are weak symbols in the HAL — overriding them here pulls the
 *  audio driver in automatically when the linker sees this TU.            */

void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    (void)hdac;
    audio_dma_half_transfer_isr();
}

void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    (void)hdac;
    audio_dma_transfer_complete_isr();
}

void HAL_DAC_ErrorCallbackCh1(DAC_HandleTypeDef *hdac)
{
    (void)hdac;
    audio_dma_error_isr();
}
