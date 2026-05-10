/**
 * @file    system_utils.h
 * @brief   System timing & utility layer for cosmo-bot (STM32L476 @ 80 MHz).
 *
 * Provides 1 ms resolution timekeeping, blocking + non-blocking delays,
 * timeout objects, microsecond delays via DWT, and an optional lightweight
 * profiler. Designed to be the single source of truth for "what time is it"
 * across the FSM, audio pipeline, OLED, UART link, and dialogue engine.
 *
 * Lifecycle:
 *   system_init();                  // call once, very early in main()
 *   uint32_t t = system_get_tick_ms();
 *   if (system_is_timeout(t0, 5000)) { ... }
 *
 * Layered design (matches spec):
 *   Layer 1 — HAL abstraction      (system_init, SysTick_Handler, DWT setup)
 *   Layer 2 — Millisecond clock    (system_get_tick_ms, system_elapsed_since)
 *   Layer 3 — Delays               (system_delay_ms / _us / _non_blocking)
 *   Layer 4 — Timeout objects      (system_timeout_t + create/check/reset)
 *   Layer 5 — Profiling (optional) (system_profile_start/stop/print)
 *
 * Thread-safety: STM32L476 is single-core, no RTOS assumed. The tick counter
 * is `volatile uint32_t` and is read/written atomically by the M4. All other
 * state is either read-only or mutated only from main-loop context.
 *
 * Wraparound: the tick counter wraps after ~49.7 days. All elapsed-time math
 * uses signed-difference arithmetic (`(int32_t)(now - then)`), which is
 * correct across the wrap boundary for elapsed intervals up to ~24 days —
 * far longer than any cosmo-bot session will ever need.
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

/** SysTick fires this many times per second (1 ms tick). */
#define SYSTEM_TICKS_PER_SECOND        1000U

/** SysTick interrupt priority. Lowest priority — never preempts UART/I2S/SPI. */
#define SYSTEM_SYSTICK_PRIORITY        15U

/** Set to 1 to enable the optional profiling layer (Layer 5). Pulls in a
 *  small static array (~320 B) and a strcmp on every start/stop. Off in
 *  production, on for benchmarking. */
#ifndef SYSTEM_PROFILING_ENABLED
#  define SYSTEM_PROFILING_ENABLED     1
#endif

/** Maximum number of distinct functions the profiler can track. */
#define SYSTEM_PROFILE_MAX_ENTRIES     10

/** Maximum length (incl. NUL) of a profile entry's function name. */
#define SYSTEM_PROFILE_NAME_MAX        24

/* ========================================================================== */
/*  Types                                                                     */
/* ========================================================================== */

/** Milliseconds since boot. Wraps after ~49.7 days. */
typedef uint32_t system_tick_t;

/**
 * Timeout object. Pass by pointer to the check/reset/remaining helpers.
 *
 * Typical use:
 *   system_timeout_t t = system_create_timeout(5000);   // 5 second budget
 *   while (!uart_message_available()) {
 *       if (system_timeout_check(&t)) { error(); break; }
 *   }
 */
typedef struct {
    system_tick_t start_time;   /**< tick when the timeout was started/reset */
    uint32_t      timeout_ms;   /**< budget in ms                            */
    uint8_t       triggered;    /**< latches to 1 once expired               */
} system_timeout_t;

/**
 * One profiler slot. Internal; exposed only so users can iterate if they
 * want a custom report. Use system_profile_print_all() for the standard
 * UART dump.
 */
typedef struct {
    char          name[SYSTEM_PROFILE_NAME_MAX]; /**< function tag           */
    system_tick_t entry_time;                    /**< most recent _start tick*/
    uint32_t      call_count;                    /**< completed calls        */
    uint32_t      total_ms;                      /**< sum of all durations   */
    uint32_t      last_ms;                       /**< most recent duration   */
    uint32_t      max_ms;                        /**< worst-case duration    */
    uint8_t       running;                       /**< 1 between start & stop */
} system_profile_t;

/* ========================================================================== */
/*  Layer 1 — HAL abstraction                                                 */
/* ========================================================================== */

/**
 * Initialize the timing subsystem.
 *  - configures SysTick for a 1 ms tick @ HCLK/8 (10 kHz reload of 10000)
 *  - enables the DWT cycle counter (used by system_delay_us)
 *  - zeroes the tick counter and the profiler
 *
 * Must be called before any other function in this module. Safe to call
 * exactly once; calling twice is a no-op.
 */
void     system_init(void);

/** Stop SysTick. All timekeeping freezes. Rarely useful — provided for
 *  low-power experiments. */
void     system_stop_systick(void);

/** Re-start SysTick after a stop. */
void     system_start_systick(void);

/**
 * SysTick exception handler. Defined here, called by the vector table at
 * 1 kHz. Increments the global tick counter and nothing else — keeps ISR
 * latency well under 1 µs.
 *
 * Declared in this header so a static analyzer can confirm it isn't a
 * stale duplicate of the weak Cube-generated symbol.
 */
void     SysTick_Handler(void);

/* ========================================================================== */
/*  Layer 2 — Millisecond clock                                               */
/* ========================================================================== */

/** Current monotonic time in ms since boot. O(1), <100 ns. */
system_tick_t system_get_tick_ms(void);

/**
 * Milliseconds elapsed since `reference_time`. Handles wraparound by using
 * signed-difference arithmetic; if `reference_time` is in the future
 * (clock skew, manual mutation), returns 0 instead of a huge value.
 */
uint32_t system_elapsed_since(system_tick_t reference_time);

/**
 * 1 if `timeout_ms` have elapsed since `start_time`, else 0.
 * Wraparound-safe. Cheap — call from a tight polling loop without worry.
 */
uint8_t  system_is_timeout(system_tick_t start_time, uint32_t timeout_ms);

/* ========================================================================== */
/*  Layer 3 — Delays                                                          */
/* ========================================================================== */

/** Spin-wait for at least `ms` milliseconds. Blocks; do not call from ISR. */
void     system_delay_ms(uint32_t ms);

/**
 * Spin-wait for at least `us` microseconds, using the DWT cycle counter.
 * Accurate to ~1 cycle (12.5 ns @ 80 MHz). Requires system_init() to have
 * enabled DWT. Safe to call from ISR if necessary, but discouraged.
 *
 * For us == 0 returns immediately.
 * For us > ~50,000 prefer system_delay_ms() — the cycle math overflows
 * if `us * 80` exceeds 2^32.
 */
void     system_delay_us(uint32_t us);

/**
 * Non-blocking periodic gate. Returns 1 when at least `interval_ms` have
 * passed since `*last_time`, and updates `*last_time` so the next tick is
 * scheduled relative to now. Returns 0 otherwise.
 *
 * Use for once-per-N-ms work in the main loop:
 *
 *   static system_tick_t t_anim = 0;
 *   if (system_delay_ms_non_blocking(&t_anim, 100)) {
 *       oled_step_animation();
 *   }
 *
 * On the very first call (`*last_time == 0`) the function fires immediately
 * and seeds `*last_time`, so the caller never has to special-case startup.
 */
uint8_t  system_delay_ms_non_blocking(system_tick_t *last_time,
                                      uint32_t interval_ms);

/* ========================================================================== */
/*  Layer 4 — Timeout objects                                                 */
/* ========================================================================== */

/** Construct a fresh timeout, armed from "now". */
system_timeout_t system_create_timeout(uint32_t timeout_ms);

/** Returns 1 the first time the timeout expires and on every call after.
 *  The `triggered` flag latches so polling loops see a consistent verdict. */
uint8_t  system_timeout_check(system_timeout_t *t);

/** Re-arm the timeout from "now" and clear the latched flag. */
void     system_timeout_reset(system_timeout_t *t);

/** Remaining budget in ms, or 0 if already expired. */
uint32_t system_timeout_remaining(const system_timeout_t *t);

/* ========================================================================== */
/*  Layer 5 — Profiling (optional)                                            */
/* ========================================================================== */

#if SYSTEM_PROFILING_ENABLED

/** Mark the start of a profiled section. `name` is matched by content; pass
 *  a string literal so the lookup is fast and stable across call sites. */
void     system_profile_start(const char *name);

/** Mark the end of a profiled section started by system_profile_start. */
void     system_profile_stop (const char *name);

/** Dump a tabulated report of every recorded profile to stdout (printf). */
void     system_profile_print_all(void);

/** Erase all recorded profile data. */
void     system_profile_reset(void);

/** Read-only access to the underlying table — for custom reporting. */
const system_profile_t *system_profile_table(size_t *out_count);

#endif /* SYSTEM_PROFILING_ENABLED */

/* ========================================================================== */
/*  Convenience macros                                                        */
/* ========================================================================== */

/** Shorthand: `SYSTEM_NOW()` reads like a clock query, not a function call. */
#define SYSTEM_NOW()                  system_get_tick_ms()

/** Shorthand: declare-and-arm a stack-local timeout. */
#define SYSTEM_TIMEOUT(name, ms)      system_timeout_t name = system_create_timeout(ms)

/** Convert seconds → ms at compile time, with overflow-safe phrasing. */
#define SYSTEM_SECONDS(s)             ((uint32_t)(s) * 1000U)

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_UTILS_H */
