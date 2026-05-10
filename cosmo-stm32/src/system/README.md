# system_utils — Phase 0 timing layer

Millisecond clock, delays, timeouts, and a tiny profiler for `cosmo-bot` on
STM32L476. Everything else (FSM, audio, OLED, UART) consumes this module.

```
cosmo-stm32/src/system/
├── system_utils.h        public API (~10 kB)
├── system_utils.c        implementation (~11 kB)
└── README.md             this file
```

## What you get

| Layer | Functions                                                             |
|------:|-----------------------------------------------------------------------|
|     1 | `system_init`, `system_start_systick`, `system_stop_systick`          |
|     2 | `system_get_tick_ms`, `system_elapsed_since`, `system_is_timeout`     |
|     3 | `system_delay_ms`, `system_delay_us`, `system_delay_ms_non_blocking`  |
|     4 | `system_create_timeout`, `_check`, `_reset`, `_remaining`             |
|     5 | `system_profile_start`/`_stop`/`_print_all`/`_reset`                  |

Convenience: `SYSTEM_GET_TIME_MS()` macro and `SYSTEM_PROFILE_BLOCK("name") { ... }`.

## CubeMX configuration

The HAL already configures SysTick during `HAL_Init()`. We layer on top of
it rather than fighting it:

- **Clock source:** SysTick is reconfigured to **HCLK / 8** by `system_init()`
  (10 MHz at the project's 80 MHz HCLK). This matches the spec and frees
  the /1 source for power-management experiments later.
- **Reload value:** `(HCLK / 8) / 1000 = 10000` for a 1 kHz interrupt.
- **Priority:** Set to **15** (lowest) so it can't pre-empt audio DMA or
  UART ISRs.
- **Hook:** We override `HAL_SYSTICK_Callback()`, *not* `SysTick_Handler()`.
  This means `HAL_GetTick()` and any HAL function that uses timeouts
  (`HAL_UART_Transmit`, etc.) keep working in parallel.

In CubeMX, no changes are required from the existing project — leave
SysTick on the default 1 kHz, and let `system_init()` retune it.

## Adding to the build

`platformio.ini` (or your Makefile) already pulls in `cosmo-stm32/src/**/*.c`
via `build_src_filter`. No project file changes needed.

In `main.c`:

```c
#include "system/system_utils.h"

int main(void) {
    HAL_Init();
    SystemClock_Config();
    /* ... CubeMX MX_*_Init() calls ... */

    system_init();                          /* <-- once, after HAL is up */

    while (1) {
        fsm_run();
    }
}
```

## Usage patterns

**FSM state timeout** (Phase 1):

```c
fsm.state_entry_time = system_get_tick_ms();
fsm.timeout_ms       = 5000;
/* ... */
if (system_is_timeout(fsm.state_entry_time, fsm.timeout_ms)) {
    fsm.next_state = FSM_STATE_ERROR;
}
```

**Periodic display update** (Phase 4, non-blocking):

```c
static uint32_t last_anim = 0;
if (system_delay_ms_non_blocking(&last_anim, 100)) {
    oled_update_animation_frame();
}
```

**UART response timeout** (Phase 5):

```c
system_timeout_t t = system_create_timeout(5000);
while (!uart_message_available()) {
    if (system_timeout_check(&t)) return UART_ERR_TIMEOUT;
}
```

**Profiling STT inference** (Phase 3):

```c
SYSTEM_PROFILE_BLOCK("stt_run") {
    stt_run(audio_buf, audio_len);
}
/* later, from a debug menu: */
system_profile_set_uart(&huart2);
system_profile_print_all();
```

## Wraparound

`uint32_t` ms wraps at ~49.7 days. All elapsed-time helpers use signed
two's-complement arithmetic, which is correct across one wraparound for
intervals up to ~24.8 days. Cosmo sessions are minutes, so this is purely
defensive — but it costs nothing and the math is right.

```c
static inline uint32_t elapsed_signed(uint32_t now, uint32_t ref) {
    int32_t d = (int32_t)(now - ref);
    return (d < 0) ? 0u : (uint32_t)d;
}
```

## Testing

After flashing a firmware that calls `system_init()` and prints the tick:

| Test                      | Expected                                            |
|---------------------------|-----------------------------------------------------|
| `system_get_tick_ms` x100 | Increments by 1 each ms                             |
| `system_delay_ms(250)`    | Returns after 250 ms ±2 ms                          |
| `system_delay_us(100)`    | Scope: GPIO toggled around the call shows 100 µs ±1 |
| Non-blocking @ 100 ms     | 5 fires in 500 ms, no CPU stall between             |
| Timeout 200 ms            | `_check` returns 0 immediately, 1 after 200 ms      |
| `_reset` mid-flight       | Sticky flag cleared, new 200 ms window from now     |
| Profile block, 5 calls    | `print_all` reports 5 calls, avg ≈ delay used       |

For SysTick frequency verification, toggle a GPIO inside
`HAL_SYSTICK_Callback()` and check for a 1 kHz square wave on a scope.

## Memory

| Symbol           | Bytes   | Note                              |
|------------------|---------|-----------------------------------|
| `system_ticks`   | 4       | volatile, ISR-written             |
| `system_config`  | ~24     | one-time configured               |
| profile table    | ~320    | only when `SYSTEM_ENABLE_PROFILING` |
| **Total RAM**    | **<1 kB** | leaves ~127 kB for everything else |

Flash cost is ~3 kB at `-Os`.

## Known limitations

- `system_delay_ms` is busy-wait; it won't enter low-power idle. Acceptable
  for Phase 0; revisit when battery work begins.
- The profiler compares function names by **pointer** (cheap), so always
  pass a string literal — `system_profile_start(my_string_var)` won't
  match a later `..._stop("my_string_var")`.
- `system_stop_systick()` also stops `HAL_GetTick()`, which breaks any
  HAL function with a timeout argument. Don't call it on the hot path.
