/**
 * audio_test_main.c — Audio driver test harness (Minimal)
 *
 * Compile with: -DAUDIO_TESTS_MAIN (defined in platformio.ini)
 * When defined, replaces main() to run audio tests instead of FSM.
 */

#ifdef AUDIO_TESTS_MAIN

#include <stdio.h>
#include "stm32l4xx_hal.h"
#include "audio_driver_tests.h"
#include "debug_uart.h"

void SystemClock_Config(void);
void GPIO_Init(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    DebugUart_Init();

    printf("\r\n");
    printf("╔═══════════════════════════════════════════════════════════╗\r\n");
    printf("║         Audio Driver Test Suite (Bring-up Guide)          ║\r\n");
    printf("╚═══════════════════════════════════════════════════════════╝\r\n");
    printf("\r\n");

    run_all_audio_tests();

    printf("\r\n");
    printf("Test suite complete. Press reset to run again.\r\n");
    printf("\r\n");

    while (1) {
        HAL_Delay(1000);
    }

    return 0;
}

#endif /* AUDIO_TESTS_MAIN */
