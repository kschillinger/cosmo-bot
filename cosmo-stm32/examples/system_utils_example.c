/**
 * @file    system_utils_example.c
 * @brief   Standalone bring-up demo for the system_utils module.
 *
 * Drop-in replacement for main.c on a fresh Nucleo-L476RG. Exercises every
 * layer of system_utils and reports the results over UART2 (the ST-Link
 * VCP — connect at 115200 8N1 to read it). Toggles LED2 (PA5) at 1 Hz so
 * you have a fast visual sanity check that timekeeping is alive.
 *
 * What you should see on the serial console:
 *
 *   --- system_utils bring-up ---
 *   [Phase 1] tick alive: 0 -> 1000 (delta=1000, expected 1000)
 *   [Phase 2] elapsed_since round-trip: 250 ms (expected 250)
 *   [Phase 3] microsecond delay: 100 us round-trip = 101 us (DWT)
 *   [Phase 4] timeout (200 ms): not_yet=0  expired=1
 *   [Phase 5] profiler: work_unit avg=100 max=100 total=500 (5 calls)
 *   [main loop] heartbeat — LED toggling every 500 ms
 *
 * If anything looks off, see cosmo-stm32/src/system/README.md →
 * "Troubleshooting".
 */

#include "system_utils.h"
#include "stm32l4xx_hal.h"

#include <stdio.h>

/* These come from CubeMX-generated code in a real project. Forward-declared
 * here so this file compiles in isolation as a reference. */
extern void SystemClock_Config(void);
extern void MX_GPIO_Init(void);
extern void MX_USART2_UART_Init(void);

/* LED2 on Nucleo-L476RG is PA5. */
#define LED_PORT   GPIOA
#define LED_PIN    GPIO_PIN_5

/* ------------------------------------------------------------------------- */

static void phase1_tick_alive(void)
{
    const system_tick_t t0 = system_get_tick_ms();
    system_delay_ms(1000);
    const system_tick_t t1 = system_get_tick_ms();
    const uint32_t delta = (uint32_t)(t1 - t0);
    printf("[Phase 1] tick alive: %lu -> %lu (delta=%lu, expected 1000)\r\n",
           (unsigned long)t0, (unsigned long)t1, (unsigned long)delta);
}

static void phase2_elapsed_round_trip(void)
{
    const system_tick_t mark = SYSTEM_NOW();
    system_delay_ms(250);
    const uint32_t elapsed = system_elapsed_since(mark);
    printf("[Phase 2] elapsed_since round-trip: %lu ms (expected 250)\r\n",
           (unsigned long)elapsed);
}

static void phase3_microsecond_delay(void)
{
    /* Time a 100 µs delay with the same DWT counter that drives it. The
     * reading is mostly self-confirmation, but it catches the case where
     * DWT was never enabled (delay returns instantly → reads ~0 µs). */
    const uint32_t cyc_start = DWT->CYCCNT;
    system_delay_us(100);
    const uint32_t cyc_end   = DWT->CYCCNT;
    const uint32_t us = (cyc_end - cyc_start) / (SystemCoreClock / 1000000U);
    printf("[Phase 3] microsecond delay: 100 us round-trip = %lu us (DWT)\r\n",
           (unsigned long)us);
}

static void phase4_timeout_object(void)
{
    SYSTEM_TIMEOUT(t, 200);
    system_delay_ms(50);
    const uint8_t not_yet = system_timeout_check(&t);
    system_delay_ms(200);
    const uint8_t expired = system_timeout_check(&t);
    printf("[Phase 4] timeout (200 ms): not_yet=%u  expired=%u\r\n",
           (unsigned)not_yet, (unsigned)expired);
}

#if SYSTEM_PROFILING_ENABLED
static void phase5_profiler(void)
{
    system_profile_reset();
    for (int i = 0; i < 5; ++i) {
        system_profile_start("work_unit");
        system_delay_ms(100);
        system_profile_stop("work_unit");
    }
    system_profile_print_all();
}
#endif

/* ------------------------------------------------------------------------- */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();

    /* Initialize timing BEFORE any code that wants to delay or stamp time. */
    system_init();

    printf("\r\n--- system_utils bring-up ---\r\n");

    phase1_tick_alive();
    phase2_elapsed_round_trip();
    phase3_microsecond_delay();
    phase4_timeout_object();
#if SYSTEM_PROFILING_ENABLED
    phase5_profiler();
#endif

    printf("\r\n[main loop] heartbeat — LED toggling every 500 ms\r\n");

    /* Demonstrate the non-blocking gate: heartbeat LED at 1 Hz, status
     * print every 5 s, both off the same main loop with no HAL_Delay(). */
    system_tick_t t_led    = 0;
    system_tick_t t_report = 0;
    uint32_t      ticks    = 0;

    for (;;) {
        if (system_delay_ms_non_blocking(&t_led, 500)) {
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
            ticks++;
        }
        if (system_delay_ms_non_blocking(&t_report, 5000)) {
            printf("[main loop] alive @ %lu ms (LED toggled %lu times)\r\n",
                   (unsigned long)SYSTEM_NOW(), (unsigned long)ticks);
        }
    }
}
