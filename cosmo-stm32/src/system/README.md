# `system_utils` — timing layer for cosmo-bot (STM32L476)

The single source of truth for "what time is it" across the FSM, audio
pipeline, OLED driver, UART link, and dialogue engine. Built on SysTick
(1 ms tick) plus the DWT cycle counter (for sub-microsecond delays).

## API at a glance

| Layer | Functions | What for |
|-------|-----------|----------|
| 1 — HAL | `system_init`, `SysTick_Handler`, `system_start_systick`, `system_stop_systick` | Bring-up |
| 2 — Clock | `system_get_tick_ms`, `system_elapsed_since`, `system_is_timeout` | "How long has it been?" |
| 3 — Delays | `system_delay_ms`, `system_delay_us`, `system_delay_ms_non_blocking` | "Wait a bit" / "fire every N ms" |
| 4 — Timeouts | `system_create_timeout`, `system_timeout_check`, `system_timeout_reset`, `system_timeout_remaining` | "Give up after T ms" |
| 5 — Profiler | `system_profile_start/stop/print_all/reset` | Benchmarking |

Convenience macros: `SYSTEM_NOW()`, `SYSTEM_TIMEOUT(name, ms)`, `SYSTEM_SECONDS(s)`.

## Quick start

```c
#include "system_utils.h"

int main(void) {
    HAL_Init();
    SystemClock_Config();        /* CubeMX-generated: 80 MHz HCLK */
    system_init();               /* must come right after HAL_Init */

    while (1) {
        static system_tick_t t_anim = 0;
        if (system_delay_ms_non_blocking(&t_anim, 100)) {
            oled_step_animation();
        }
        fsm_run();
    }
}
```

## Integration with subsystems

- **FSM** — every state stamps `state_entry_time = SYSTEM_NOW()` on entry,
  then polls `system_is_timeout(state_entry_time, state_timeout_ms)` from
  its tick handler. See `cosmo-stm32/src/fsm.c`.
- **Audio capture** — silence-detection runs from a non-blocking 100 ms
  gate; recordings are wall-clocked with `system_elapsed_since()`.
- **OLED** — animation frames advance on a 100 ms non-blocking gate so
  the display never stalls the CPU.
- **UART link** — every request to the ESP32 arms a 5 s `system_timeout_t`;
  on expiry the FSM bails to its error state.

## CubeMX notes

CubeMX generates a weak `SysTick_Handler` that calls `HAL_IncTick()` only.
This module overrides that symbol with a stronger definition that does
**both** — it increments our own counter *and* calls `HAL_IncTick()` so any
HAL code that still uses `HAL_Delay()` keeps working.

In CubeMX:

1. **System Core → SYS → Timebase Source** → leave as `SysTick`. Our
   handler subsumes HAL's.
2. **Clock Configuration** → confirm HCLK = 80 MHz. The reload value is
   computed from `SystemCoreClock` so any clock change is picked up
   automatically — but if you ever drop below 8 MHz HCLK the
   reload won't fit cleanly and you'll need to revisit the divider.
3. **NVIC** → SysTick priority is set in code to 15 (lowest). Don't fight
   it from the GUI.

## Testing checklist

| Phase | Test | Expected |
|-------|------|----------|
| 1 | `system_init()`; toggle a GPIO in `SysTick_Handler`; scope it | 1.000 kHz square wave |
| 2 | `t = SYSTEM_NOW()`; `HAL_Delay(1000)`; `SYSTEM_NOW() - t` | 1000 ± 1 |
| 3 | `system_delay_ms(250)` round-trip | 250 ± 2 ms |
| 3 | `system_delay_us(100)` w/ scope on a toggling pin | 100 ± 0.1 µs |
| 4 | `t = system_create_timeout(200); system_delay_ms(150); !system_timeout_check(&t)` | true |
| 4 | `system_delay_ms(100); system_timeout_check(&t)` | true |
| 5 | profile a function called 5× w/ `system_delay_ms(100)`; print | avg ≈ 100, total ≈ 500 |

## Wraparound

`system_tick_t` wraps after 49.7 days. All elapsed math uses
`(int32_t)(now - then)`, which is correct across the wrap for elapsed
intervals up to ~24 days. Cosmo-bot sessions are minutes; this is more
than enough headroom and behaves correctly even if you leave the device
on for a month.

## Troubleshooting

- **Tick not advancing** — almost always a duplicate `SysTick_Handler`.
  CubeMX puts a weak one in `stm32l4xx_it.c`; ours in `system_utils.c`
  takes precedence at link time. If you see a multiple-definition error,
  delete the body of the Cube one (leave the function, just empty it out)
  or add `__attribute__((weak))` there.
- **`system_delay_us` returns instantly** — DWT not enabled. `system_init`
  enables it, so this means `system_init` was never called.
- **Delays are ~8× too short or too long** — `SYSTICK_CLKSOURCE_HCLK_DIV8`
  vs `SYSTICK_CLKSOURCE_HCLK` mismatch. We use `/8`; the reload is computed
  to match.

## Memory footprint

Roughly 360 B of RAM (320 for the profiler table when enabled, plus
counters). Set `SYSTEM_PROFILING_ENABLED` to 0 in production to drop the
profiler entirely.
