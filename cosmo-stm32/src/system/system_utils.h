/*
 * system_utils.h — millisecond timekeeping, delays, timeouts, profiling
 *
 * Target:   STM32L476 @ 80 MHz, SysTick @ HCLK/8 (10 MHz) -> 1 kHz tick
 * Project:  cosmo-bot — conversational chatbot, system layer
 *
 * Layered design (matches spec):
 *   Layer 1 — Hardware init (SysTick, optional DWT cycle counter)
 *   Layer 2 — Millisecond clock (volatile uint32_t tick counter)
 *   Layer 3 — Delays (blocking ms, microsecond via DWT, non-blocking)
 *   Layer 4 — Timeout objects (create/check/reset/remaining)
 *   Layer 5 — Performance profiling (start/stop, table, UART print)
 *   Layer 6 — Public API (re-exports of the above)
 *
 * Wraparound:
 *   The tick counter is uint32_t and wraps after ~49.7 days. All
 *   elapsed-time calculations use signed-difference arithmetic, which
 *   stays correct across one wraparound for any interval up to INT32_MAX
 *   (~24.8 days). Cosmo sessions are minutes, so this is always safe in
 *   practice — but the math is right regardless.
 *
 * Memory:
 *   tick counter           4 B   (volatile, written by ISR)
 *   config struct        ~24 B
 *   profile table        ~320 B (10 entries, only if profiling enabled)
 *   ----
 *   Total                <1 kB
 */

#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Compile-time configuration                                                */
/* ========================================================================== */

#define SYSTEM_TICKS_PER_SECOND         1000U      /* 1 ms tick               */
#define SYSTEM_DEFAULT_CLOCK_HZ         80000000U  /* 80 MHz on STM32L476     */

/* Enable DWT cycle counter for microsecond delays. Always available on
 * Cortex-M4. Disable only if you need to free DWT for another use.          */
#ifndef SYSTEM_ENABLE_DWT
#  define SYSTEM_ENABLE_DWT             1
#endif

/* Enable performance profiling table. Adds ~320 B RAM and a small per-call
 * lookup cost. Define to 0 in shipping firmware if you don't need it.       */
#ifndef SYSTEM_ENABLE_PROFILING
#  define SYSTEM_ENABLE_PROFILING       1
#endif

#define SYSTEM_MAX_PROFILES             10

/* ========================================================================== */
/*  Types                                                                     */
/* ========================================================================== */

typedef uint32_t system_tick_t;     /* milliseconds since boot                */

typedef struct {
    uint32_t  ticks_per_second;     /* 1000                                   */
    uint32_t  system_clock_hz;      /* HCLK in Hz                             */
    uint32_t  startup_time;         /* tick value at system_init()            */
    uint8_t   systick_enabled;      /* 1 once SysTick is running              */
    uint8_t   dwt_enabled;          /* 1 if DWT cycle counter usable          */
} system_config_t;

typedef struct {
    uint32_t  start_time;           /* tick when timeout was created/reset    */
    uint32_t  timeout_ms;           /* duration                               */
    uint8_t   triggered;            /* sticky flag once expired               */
} system_timeout_t;

#if SYSTEM_ENABLE_PROFILING
typedef struct {
    const char *function_name;      /* identity (compared by pointer; pass    */
                                    /* a string literal for stable identity) */
    uint32_t    entry_time;
    uint32_t    exit_time;
    uint32_t    duration_ms;        /* most recent call                       */
    uint32_t    call_count;
    uint32_t    total_time;         /* sum of all call durations              */
} system_profile_t;
#endif

/* ========================================================================== */
/*  Globals — read-only from outside this module; the ISR writes the tick.    */
/* ========================================================================== */

extern volatile system_tick_t system_ticks;
extern system_config_t        system_config;

/* ========================================================================== */
/*  Layer 1 — Hardware abstraction                                            */
/* ========================================================================== */

/**
 * Initialise the timing system. Must be called once after HAL_Init() and
 * SystemClock_Config(), before any other system_*() call.
 *
 * Configures SysTick for a 1 kHz interrupt (HCLK/8 divider, reload =
 * HCLK/8/1000), enables the DWT cycle counter (if SYSTEM_ENABLE_DWT),
 * zeroes the tick counter, and stamps startup_time.
 */
void system_init(void);

/** Re-arm SysTick after stop. Rarely needed. */
void system_start_systick(void);

/**
 * Disable SysTick. Stops all timekeeping until system_start_systick().
 * Use only on low-power/sleep paths — and remember HAL_GetTick() also
 * stops, which breaks HAL_Delay() and any HAL function with a timeout.
 */
void system_stop_systick(void);

/* ========================================================================== */
/*  Layer 2 — Millisecond clock                                               */
/* ========================================================================== */

/**
 * Current monotonic time in milliseconds since boot.
 * O(1), single 32-bit load. Safe to call from main and ISR context.
 */
uint32_t system_get_tick_ms(void);

/** Synonym for system_get_tick_ms(). Provided to mirror HAL_GetTick(). */
uint32_t system_get_tick_count(void);

/**
 * Milliseconds elapsed since reference_time, with correct handling of one
 * wraparound. Always returns a non-negative count.
 */
uint32_t system_elapsed_since(uint32_t reference_time);

/** 1 if (now - start_time) >= timeout_ms, else 0. Wraparound-safe. */
uint8_t system_is_timeout(uint32_t start_time, uint32_t timeout_ms);

/* ========================================================================== */
/*  Layer 3 — Delays                                                          */
/* ========================================================================== */

/**
 * Block the CPU for at least `milliseconds` ms. Spin-waits on the tick
 * counter; do NOT call from ISR context — the tick can't advance there.
 */
void system_delay_ms(uint32_t milliseconds);

/**
 * Block the CPU for `microseconds` µs using the DWT cycle counter
 * (~50 ns precision). Falls back to a calibrated busy loop (~10% accuracy)
 * if SYSTEM_ENABLE_DWT is 0 or DWT init failed.
 */
void system_delay_us(uint32_t microseconds);

/**
 * Non-blocking interval gate. *last_time should persist between calls
 * (typically `static`). Returns 1 and updates *last_time when at least
 * `interval_ms` has elapsed; otherwise returns 0 and leaves *last_time
 * alone.
 *
 * Use in main loops for periodic work without sleeping the CPU. Example:
 *
 *     static uint32_t last = 0;
 *     if (system_delay_ms_non_blocking(&last, 100)) {
 *         oled_update_animation_frame();
 *     }
 */
uint8_t system_delay_ms_non_blocking(uint32_t *last_time, uint32_t interval_ms);

/* ========================================================================== */
/*  Layer 4 — Timeout objects                                                 */
/* ========================================================================== */

/** Create a new timeout, armed from now. */
system_timeout_t system_create_timeout(uint32_t timeout_ms);

/**
 * 1 if the timeout has fired, else 0. Sticky: once it fires, every
 * subsequent call returns 1 until system_timeout_reset().
 */
uint8_t system_timeout_check(system_timeout_t *timeout);

/** Re-arm the timeout, clearing the sticky flag. */
void system_timeout_reset(system_timeout_t *timeout);

/** Remaining ms until expiry; 0 if already expired. */
uint32_t system_timeout_remaining(system_timeout_t *timeout);

/* ========================================================================== */
/*  Layer 5 — Performance profiling                                           */
/* ========================================================================== */

#if SYSTEM_ENABLE_PROFILING

/**
 * Mark function entry. `function_name` MUST be a string literal (or any
 * other stable pointer): the table compares by pointer identity, not by
 * strcmp, to keep the hot path cheap.
 */
void system_profile_start(const char *function_name);

/** Mark function exit. Updates duration / call_count / total_time. */
void system_profile_stop(const char *function_name);

/** Wire up the UART used by system_profile_print_all(). */
void system_profile_set_uart(UART_HandleTypeDef *huart);

/**
 * Print the profile table over the configured UART. No-op if no UART has
 * been registered. Output is text, ~80 cols, CRLF-terminated.
 */
void system_profile_print_all(void);

/** Clear all entries. */
void system_profile_reset(void);

#endif /* SYSTEM_ENABLE_PROFILING */

/* ========================================================================== */
/*  Convenience macros                                                        */
/* ========================================================================== */

/* Inline current-tick read where a function call is overkill. */
#define SYSTEM_GET_TIME_MS()        ((uint32_t)system_ticks)

/*
 * Profile-block helper. Wraps a code block with start/stop calls. Usage:
 *
 *     SYSTEM_PROFILE_BLOCK("stt_run") {
 *         stt_run(buf, len);
 *     }
 */
#if SYSTEM_ENABLE_PROFILING
#  define SYSTEM_PROFILE_BLOCK(name)                                          \
        for (int _sp_once = (system_profile_start(name), 1);                  \
             _sp_once;                                                        \
             _sp_once = 0, system_profile_stop(name))
#else
#  define SYSTEM_PROFILE_BLOCK(name)  /* compiled out */
#endif

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_UTILS_H */
