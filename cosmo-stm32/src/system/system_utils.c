/**
 * @file    system_utils.c
 * @brief   Implementation of the system timing layer for cosmo-bot.
 *
 * See system_utils.h for the API description and design rationale.
 *
 * Notes for future maintainers:
 *   - The tick counter is the *only* mutable state touched from interrupt
 *     context (SysTick_Handler). Everything else lives in main-loop land.
 *   - Elapsed-time math uses signed differences so the 49-day wraparound
 *     is correct without any explicit branch.
 *   - The profiler does a small linear scan + strncmp on each start/stop;
 *     with SYSTEM_PROFILE_MAX_ENTRIES = 10 this costs ~250 ns and is fine
 *     even inside hot inner loops.
 */

#include "system_utils.h"

#include <string.h>
#include <stdio.h>

/* ========================================================================== */
/*  Module state                                                              */
/* ========================================================================== */

/* The one true clock. `volatile` because SysTick_Handler writes it and the
 * main loop reads it; without `volatile` the compiler is allowed to cache
 * the value across loop iterations, which would freeze every wait loop. */
static volatile system_tick_t s_ticks_ms = 0;

/* Set by system_init(); read by system_init() to make the call idempotent. */
static uint8_t  s_initialized = 0;

/* CPU cycles per microsecond. Cached from SystemCoreClock at init time so
 * system_delay_us() doesn't have to divide on every call. */
static uint32_t s_cycles_per_us = 80;   /* default for 80 MHz; refined in init */

/* DWT register block. Available on every Cortex-M4 with the trace unit
 * present (true for all STM32L4). Names match the ARM ARM. */
#define DWT_CTRL_CYCCNTENA_BIT_MSK   (1U << 0)
#define COREDEBUG_DEMCR_TRCENA_MSK   (1U << 24)

/* ========================================================================== */
/*  Layer 1 — HAL abstraction                                                 */
/* ========================================================================== */

void system_init(void)
{
    if (s_initialized) {
        return;                        /* idempotent: harmless second call    */
    }

    /* --- SysTick: 1 ms tick from HCLK / 8 ----------------------------------
     * On STM32L476 with HCLK = 80 MHz, the SysTick clock is 10 MHz.
     * Reload = 10 MHz / 1 kHz = 10 000 ticks → 1.000 ms period exactly.    */
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK_DIV8);
    const uint32_t reload = (SystemCoreClock / 8U) / SYSTEM_TICKS_PER_SECOND;

    /* SysTick_Config returns 0 on success; if it fails the reload was too
     * large for the 24-bit counter, which can't happen at 80 MHz/8/1kHz. */
    (void)SysTick_Config(reload);

    /* Lowest priority — never preempts UART/SPI/I2S DMA completion ISRs.   */
    HAL_NVIC_SetPriority(SysTick_IRQn, SYSTEM_SYSTICK_PRIORITY, 0);

    /* --- DWT cycle counter: enables ns-precision delays --------------------
     * Two bits to flip: TRCENA in CoreDebug->DEMCR (gates the whole DWT
     * block), and CYCCNTENA in DWT->CTRL (starts the cycle counter).      */
    CoreDebug->DEMCR |= COREDEBUG_DEMCR_TRCENA_MSK;
    DWT->CYCCNT       = 0U;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_BIT_MSK;

    s_cycles_per_us = SystemCoreClock / 1000000U;
    s_ticks_ms      = 0;
    s_initialized   = 1;
}

void system_stop_systick(void)
{
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
}

void system_start_systick(void)
{
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
}

/*
 * SysTick interrupt handler. Cube-generated projects ship a weak default
 * that just calls HAL_IncTick() — we override that with our own counter
 * and call HAL_IncTick() too so HAL_Delay() etc. still work for any code
 * that uses them.
 */
void SysTick_Handler(void)
{
    s_ticks_ms++;
    HAL_IncTick();
}

/* ========================================================================== */
/*  Layer 2 — Millisecond clock                                               */
/* ========================================================================== */

system_tick_t system_get_tick_ms(void)
{
    /* 32-bit aligned read on a 32-bit MCU is atomic; no critical section
     * needed. The `volatile` qualifier prevents the compiler from caching. */
    return s_ticks_ms;
}

uint32_t system_elapsed_since(system_tick_t reference_time)
{
    /* Signed-difference trick: works across the wrap boundary. If reference
     * is "in the future" (caller bug, or fresh-armed-but-not-yet-ticked),
     * elapsed is negative — clamp to 0 so callers never see absurd values. */
    const int32_t elapsed = (int32_t)(system_get_tick_ms() - reference_time);
    return (elapsed < 0) ? 0U : (uint32_t)elapsed;
}

uint8_t system_is_timeout(system_tick_t start_time, uint32_t timeout_ms)
{
    return (system_elapsed_since(start_time) >= timeout_ms) ? 1U : 0U;
}

/* ========================================================================== */
/*  Layer 3 — Delays                                                          */
/* ========================================================================== */

void system_delay_ms(uint32_t ms)
{
    const system_tick_t start = system_get_tick_ms();
    while (system_elapsed_since(start) < ms) {
        /* WFI would save power but also stops if any IRQ fires — and we
         * have a 1 kHz SysTick IRQ, so it would wake every ms anyway.
         * Plain spin is simpler and the energy cost is identical here. */
        __NOP();
    }
}

void system_delay_us(uint32_t us)
{
    if (us == 0U) {
        return;
    }

    /* DWT->CYCCNT is a 32-bit free-running counter at SystemCoreClock.
     * Subtraction handles its 53-second wrap automatically. */
    const uint32_t start  = DWT->CYCCNT;
    const uint32_t target = us * s_cycles_per_us;

    while ((DWT->CYCCNT - start) < target) {
        __NOP();
    }
}

uint8_t system_delay_ms_non_blocking(system_tick_t *last_time,
                                     uint32_t interval_ms)
{
    if (last_time == NULL) {
        return 0U;
    }

    const system_tick_t now = system_get_tick_ms();

    /* First-call seeding: a fresh static `system_tick_t = 0` should fire
     * immediately, not wait `interval_ms` after boot. We treat 0 as "never
     * fired yet". This costs us nothing — t==0 lasts only the first ms. */
    if (*last_time == 0U) {
        *last_time = now;
        return 1U;
    }

    if ((uint32_t)(now - *last_time) >= interval_ms) {
        *last_time = now;
        return 1U;
    }
    return 0U;
}

/* ========================================================================== */
/*  Layer 4 — Timeout objects                                                 */
/* ========================================================================== */

system_timeout_t system_create_timeout(uint32_t timeout_ms)
{
    system_timeout_t t = {
        .start_time = system_get_tick_ms(),
        .timeout_ms = timeout_ms,
        .triggered  = 0U,
    };
    return t;
}

uint8_t system_timeout_check(system_timeout_t *t)
{
    if (t == NULL) {
        return 0U;
    }
    if (t->triggered) {
        return 1U;                 /* latched — once expired, stays expired   */
    }
    if (system_elapsed_since(t->start_time) >= t->timeout_ms) {
        t->triggered = 1U;
        return 1U;
    }
    return 0U;
}

void system_timeout_reset(system_timeout_t *t)
{
    if (t == NULL) {
        return;
    }
    t->start_time = system_get_tick_ms();
    t->triggered  = 0U;
}

uint32_t system_timeout_remaining(const system_timeout_t *t)
{
    if (t == NULL) {
        return 0U;
    }
    const uint32_t elapsed = system_elapsed_since(t->start_time);
    return (elapsed >= t->timeout_ms) ? 0U : (t->timeout_ms - elapsed);
}

/* ========================================================================== */
/*  Layer 5 — Profiling                                                       */
/* ========================================================================== */

#if SYSTEM_PROFILING_ENABLED

static system_profile_t s_profiles[SYSTEM_PROFILE_MAX_ENTRIES];
static size_t           s_profile_count = 0;

/* Find an entry by name, or allocate one. Returns NULL if the table is full
 * — caller treats that as silent drop, since profiling is best-effort. */
static system_profile_t *profile_find_or_create(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < s_profile_count; ++i) {
        if (strncmp(s_profiles[i].name, name, SYSTEM_PROFILE_NAME_MAX) == 0) {
            return &s_profiles[i];
        }
    }

    if (s_profile_count >= SYSTEM_PROFILE_MAX_ENTRIES) {
        return NULL;               /* table full; drop quietly                */
    }

    system_profile_t *p = &s_profiles[s_profile_count++];
    memset(p, 0, sizeof(*p));
    strncpy(p->name, name, SYSTEM_PROFILE_NAME_MAX - 1);
    p->name[SYSTEM_PROFILE_NAME_MAX - 1] = '\0';
    return p;
}

void system_profile_start(const char *name)
{
    system_profile_t *p = profile_find_or_create(name);
    if (p == NULL) {
        return;
    }
    p->entry_time = system_get_tick_ms();
    p->running    = 1U;
}

void system_profile_stop(const char *name)
{
    system_profile_t *p = profile_find_or_create(name);
    if (p == NULL || !p->running) {
        return;                    /* stop without matching start — ignore   */
    }
    const uint32_t dur = system_elapsed_since(p->entry_time);
    p->last_ms   = dur;
    p->total_ms += dur;
    p->call_count++;
    if (dur > p->max_ms) {
        p->max_ms = dur;
    }
    p->running = 0U;
}

void system_profile_print_all(void)
{
    /* printf is wired up to UART2 in cosmo-stm32 (see retarget.c).          */
    printf("\r\n--- system_utils profiler ---\r\n");
    printf("%-24s %8s %10s %10s %10s\r\n",
           "function", "calls", "avg_ms", "max_ms", "total_ms");
    printf("---------------------------------------------------------------------\r\n");

    for (size_t i = 0; i < s_profile_count; ++i) {
        const system_profile_t *p = &s_profiles[i];
        const uint32_t avg = (p->call_count > 0U)
                                 ? (p->total_ms / p->call_count) : 0U;
        printf("%-24s %8lu %10lu %10lu %10lu\r\n",
               p->name,
               (unsigned long)p->call_count,
               (unsigned long)avg,
               (unsigned long)p->max_ms,
               (unsigned long)p->total_ms);
    }
    printf("---------------------------------------------------------------------\r\n");
}

void system_profile_reset(void)
{
    memset(s_profiles, 0, sizeof(s_profiles));
    s_profile_count = 0;
}

const system_profile_t *system_profile_table(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = s_profile_count;
    }
    return s_profiles;
}

#endif /* SYSTEM_PROFILING_ENABLED */
