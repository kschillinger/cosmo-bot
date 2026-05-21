/**
 * Cosmo Bot — STM32L476RG (Nucleo-64)
 * ============================================================================
 * Phase 2 skeleton: top-level FSM bring-up.
 *
 * Responsibilities (this phase):
 *   - 80 MHz SYSCLK (HSI16 + PLL), GPIO + USART2 (printf debug) + USART1 (ESP32)
 *   - Initialize the orchestration FSM (fsm.c) which sequences capture,
 *     STT, ESP32 round-trip, TTS, and playback through subsystem stubs.
 *
 * The Phase 1 button-test loop has been replaced. B1 (PC13) is now consumed
 * by the FSM (system_button_pressed_edge()) to leave IDLE.
 *
 * Pin map (Nucleo-L476RG):
 *   PA2  USART2_TX  -> ST-Link VCP (printf debug)
 *   PA3  USART2_RX  <- ST-Link VCP
 *   PA5  LD2 (green LED, output)
 *   PA9  USART1_TX  -> ESP32 GPIO5 (Serial1 RX)    [CN10-21]
 *   PA10 USART1_RX  <- ESP32 GPIO4 (Serial1 TX)    [CN10-33]
 *   PC13 B1 user button (input, active low)
 *
 * Build / flash:
 *   pio run -e nucleo_l476rg -t upload
 *   pio device monitor -e nucleo_l476rg
 * ============================================================================
 */

#include "stm32l4xx_hal.h"
#include "debug_uart.h"
#include "fsm.h"
#include "system_utils.h"

#include <stdio.h>

/* ---- Pin defines --------------------------------------------------------- */
#define BUTTON_PORT     GPIOC
#define BUTTON_PIN      GPIO_PIN_13
#define LED_PORT        GPIOA
#define LED_PIN         GPIO_PIN_5

/* ---- Forward declarations ------------------------------------------------ */
static void GPIO_Init(void);
void Error_Handler(void);

/* ========================================================================== */
#ifndef AUDIO_TESTS_MAIN
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    DebugUart_Init();           /* retargets printf() to USART2 / ST-Link VCP  */

    printf("\r\n");
    printf("==========================================\r\n");
    printf(" Cosmo Bot — STM32L476RG  Phase 2 (FSM)\r\n");
    printf(" SYSCLK = %lu Hz\r\n",
           (unsigned long)HAL_RCC_GetSysClockFreq());
    printf(" Press B1 to start a conversational round.\r\n");
    printf("==========================================\r\n");

    fsm_init();                 /* brings up audio/stt/uart/tts/oled stubs    */

    /* Main FSM loop. ~10 ms tick gives smooth animation and keeps CPU idle.  */
    while (1) {
        fsm_run();
        system_delay_ms(10);
    }
}
#endif

/* ========================================================================== */
/* Clock: HSI16 -> PLL -> 80 MHz SYSCLK                                       */
/* (Moved to system_init.c)                                                   */
/* ========================================================================== */

/* ========================================================================== */

/* ========================================================================== */
/* Required interrupt handlers                                                */
/* ========================================================================== */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

void NMI_Handler(void)        { while (1) {} }
void HardFault_Handler(void)  { while (1) {} }
void MemManage_Handler(void)  { while (1) {} }
void BusFault_Handler(void)   { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void SVC_Handler(void)        {}
void DebugMon_Handler(void)   {}
void PendSV_Handler(void)     {}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        /* Park here on fatal error. Attach debugger to inspect. */
    }
}
