/*
 * system_utils.c — implementation. See system_utils.h for the contract.
 *
 * Notes on integration with the STM32 HAL:
 *
 *   The HAL ships its own SysTick_Handler (in stm32l4xx_it.c) which calls
 *   HAL_IncTick() and then the weak HAL_SYSTICK_Callback(). To stay
 *   friendly with CubeMX-generated code we override the *callback*, not
 *   the handler itself — that way HAL_GetTick() and any HAL_Delay() call
 *   keep working in parallel with our own counter.
 *
 *   If you choose to delete the HAL's SysTick_Handler and provide your
 *   own, just rename HAL_SYSTICK_Callback below to SysTick_Handler; the
 *   rest of the file is unaffected.
 */

#include "system_utils.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================== */
/*  Globals                                                                   */
/* ========================================================================== */

volatile system_tick_t system_ticks = 0;

system_config_t system_config = {
    .ticks_per_second = SYSTEM_TICKS_PER_SECOND,
    .system_clock_hz  = SYSTEM_DEFAULT_CLOCK_HZ,
    .startup_time     = 0,
    .systick_enabled  = 0,
    .dwt_enabled      = 0,
};

#if SYSTEM_ENABLE_PROFILING
static system_profile_t      s_profiles[SYSTEM_MAX_PROFILES];
static uint8_t               s_profile_count = 0;
static UART_HandleTypeDef   *s_profile_huart = NULL;
#endif

/* ========================================================================== */
/*  Internal helpers                                                          */
/* ========================================================================== */

/*
 * Signed-difference elapsed time. Correct across one uint32_t wraparound
 * for any interval up to INT32_MAX (~24.8 days). Never returns negative.
 *
 * Why this works: in two's complement, (uint32_t)(a - b) reinterpreted as
 * int32_t gives the signed circular distance from b to a, modulo 2^32.
 * As long as the true elapsed interval fits in 31 bits, the result is the
 * actual elapsed count regardless of where the rollover landed.
 */
static inline uint32_t elapsed_signed(uint32_t now, uint32_t ref)
{
    int32_t d = (int32_t)(now - ref);
    return (d < 0) ? 0u : (uint32_t)d;
}

#if SYSTEM_ENABLE_DWT
static void dwt_init(void)
{
    /* Trace subsystem must be on before the cycle counter is writable. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT       = 0u;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
    system_config.dwt_enabled = 1u;
}
#endif

/* ========================================================================== */
/*  Layer 1 — Hardware abstraction                                            */
/* ========================================================================== */

void system_init(void)
{
    /* Trust the value the CMSIS startup computed from the actual PLL
     * configuration, rather than our compile-time constant. */
    SystemCoreClockUpdate();
    system_config.system_clock_hz  = SystemCoreClock;
    system_config.ticks_per_second = SYSTEM_TICKS_PER_SECOND;

    /*
     * Configure SysTick at 1 kHz from HCLK/8 per the spec:
     *   reload = (HCLK / 8) / 1000   e.g. 80 MHz -> 10000
     *
     * SysTick_Config() returns 0 on success and: writes the reload, clears
     * the current-value register, sets clock source to processor clock,
     * enables the counter, and enables the interrupt. We then *override*
     * the source back to HCLK/8 with HAL_SYSTICK_CLKSourceConfig() — it
     * has to come second, because SysTick_Config sets it to /1.
     */
    const uint32_t reload = (SystemCoreClock / 8u) / SYSTEM_TICKS_PER_SECOND;
    if (SysTick_Config(reload) == 0u) {
        HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK_DIV8);
        system_config.systick_enabled = 1u;
    }
    HAL_NVIC_SetPriority(SysTick_IRQn, 15u, 0u);

    system_ticks               = 0u;
    system_config.startup_time = 0u;

#if SYSTEM_ENABLE_DWT
    dwt_init();
#endif

#if SYSTEM_ENABLE_PROFILING
    system_profile_reset();
#endif
}

void system_start_systick(void)
{
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk;
    system_config.systick_enabled = 1u;
}

void system_stop_systick(void)
{
    SysTick->CTRL &= ~(SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk);
    system_config.systick_enabled = 0u;
}

/*
 * SysTick periodic hook. Called by the HAL's SysTick_Handler at 1 kHz.
 * Keep this short — heavy work belongs in the main loop.
 */
void HAL_SYSTICK_Callback(void)
{
    system_ticks++;
}

/* ========================================================================== */
/*  Layer 2 — Millisecond clock                                               */
/* ========================================================================== */

uint32_t system_get_tick_ms(void)
{
    /* Single aligned 32-bit load; atomic on Cortex-M4 — no need to mask. */
    return (uint32_t)system_ticks;
}

uint32_t system_get_tick_count(void)
{
    return (uint32_t)system_ticks;
}

uint32_t system_elapsed_since(uint32_t reference_time)
{
    return elapsed_signed((uint32_t)system_ticks, reference_time);
}

uint8_t system_is_timeout(uint32_t start_time, uint32_t timeout_ms)
{
    return (elapsed_signed((uint32_t)system_ticks, start_time) >= timeout_ms)
                ? 1u : 0u;
}

/* ========================================================================== */
/*  Layer 3 — Delays                                                          */
/* ========================================================================== */

void system_delay_ms(uint32_t milliseconds)
{
    const uint32_t start = (uint32_t)system_ticks;
    while (elapsed_signed((uint32_t)system_ticks, start) < milliseconds) {
        __NOP();
    }
}

void system_delay_us(uint32_t microseconds)
{
#if SYSTEM_ENABLE_DWT
    if (system_config.dwt_enabled) {
        const uint32_t cycles_per_us = SystemCoreClock / 1000000u;
        const uint32_t target        = microseconds * cycles_per_us;
        const uint32_t start         = DWT->CYCCNT;
        /* DWT->CYCCNT - start naturally wraps mod 2^32, so this is
         * correct across cycle-counter rollover (every ~53 s @ 80 MHz). */
        while ((DWT->CYCCNT - start) < target) {
            __NOP();
        }
        return;
    }
#endif
    /* Fallback: calibrated busy loop, ~10% accurate. The /4 divisor is
     * an empirical estimate of cycles-per-iteration of an unrolled NOP
     * loop on Cortex-M4 at -O2; tune for your build settings. */
    const uint32_t cycles_per_us = SystemCoreClock / 1000000u;
    const uint32_t loops         = (microseconds * cycles_per_us) / 4u;
    for (volatile uint32_t i = 0; i < loops; i++) { __NOP(); }
}

uint8_t system_delay_ms_non_blocking(uint32_t *last_time, uint32_t interval_ms)
{
    if (last_time == NULL) return 0u;
    const uint32_t now = (uint32_t)system_ticks;
    if (elapsed_signed(now, *last_time) >= interval_ms) {
        *last_time = now;
        return 1u;
    }
    return 0u;
}

/* ========================================================================== */
/*  Layer 4 — Timeout objects                                                 */
/* ========================================================================== */

system_timeout_t system_create_timeout(uint32_t timeout_ms)
{
    system_timeout_t t;
    t.start_time = (uint32_t)system_ticks;
    t.timeout_ms = timeout_ms;
    t.triggered  = 0u;
    return t;
}

uint8_t system_timeout_check(system_timeout_t *timeout)
{
    if (timeout == NULL)    return 1u;  /* fail-safe: claim expired */
    if (timeout->triggered) return 1u;
    if (elapsed_signed((uint32_t)system_ticks, timeout->start_time)
            >= timeout->timeout_ms) {
        timeout->triggered = 1u;
        return 1u;
    }
    return 0u;
}

void system_timeout_reset(system_timeout_t *timeout)
{
    if (timeout == NULL) return;
    timeout->start_time = (uint32_t)system_ticks;
    timeout->triggered  = 0u;
}

uint32_t system_timeout_remaining(system_timeout_t *timeout)
{
    if (timeout == NULL) return 0u;
    const uint32_t e = elapsed_signed((uint32_t)system_ticks,
                                       timeout->start_time);
    return (e >= timeout->timeout_ms) ? 0u : (timeout->timeout_ms - e);
}

/* ========================================================================== */
/*  Layer 5 — Performance profiling                                           */
/* ========================================================================== */

#if SYSTEM_ENABLE_PROFILING

static system_profile_t *profile_find(const char *name)
{
    for (uint8_t i = 0; i < s_profile_count; i++) {
        if (s_profiles[i].function_name == name) {
            return &s_profiles[i];
        }
    }
    return NULL;
}

static system_profile_t *profile_create(const char *name)
{
    if (s_profile_count >= SYSTEM_MAX_PROFILES) return NULL;
    system_profile_t *p = &s_profiles[s_profile_count++];
    p->function_name = name;
    p->entry_time    = 0u;
    p->exit_time     = 0u;
    p->duration_ms   = 0u;
    p->call_count    = 0u;
    p->total_time    = 0u;
    return p;
}

void system_profile_start(const char *function_name)
{
    if (function_name == NULL) return;
    system_profile_t *p = profile_find(function_name);
    if (p == NULL) p = profile_create(function_name);
    if (p == NULL) return;  /* table full — drop silently */
    p->entry_time = (uint32_t)system_ticks;
}

void system_profile_stop(const char *function_name)
{
    if (function_name == NULL) return;
    system_profile_t *p = profile_find(function_name);
    if (p == NULL) return;
    p->exit_time   = (uint32_t)system_ticks;
    p->duration_ms = elapsed_signed(p->exit_time, p->entry_time);
    p->call_count += 1u;
    p->total_time += p->duration_ms;
}

void system_profile_set_uart(UART_HandleTypeDef *huart)
{
    s_profile_huart = huart;
}

static void profile_uart_print(const char *s)
{
    if (s_profile_huart == NULL || s == NULL) return;
    HAL_UART_Transmit(s_profile_huart, (uint8_t *)s,
                      (uint16_t)strlen(s), 100u);
}

void system_profile_print_all(void)
{
    if (s_profile_huart == NULL) return;
    char line[96];

    profile_uart_print("\r\n--- system profile ---\r\n");
    profile_uart_print("function           calls   avg(ms)   total(ms)\r\n");
    for (uint8_t i = 0; i < s_profile_count; i++) {
        const system_profile_t *p = &s_profiles[i];
        const uint32_t avg = (p->call_count > 0u) ?
                              (p->total_time / p->call_count) : 0u;
        snprintf(line, sizeof(line),
                 "%-18s %5lu  %8lu  %10lu\r\n",
                 p->function_name ? p->function_name : "(null)",
                 (unsigned long)p->call_count,
                 (unsigned long)avg,
                 (unsigned long)p->total_time);
        profile_uart_print(line);
    }
    profile_uart_print("----------------------\r\n");
}

void system_profile_reset(void)
{
    memset(s_profiles, 0, sizeof(s_profiles));
    s_profile_count = 0u;
}

#endif /* SYSTEM_ENABLE_PROFILING */
