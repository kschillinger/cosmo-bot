# Audio Driver Test Build Fix — Summary

## Problem Identified
The previous build configuration had conflicting `main()` function definitions:
- `src/main.c` — Original FSM entry point  
- `src/audio_test_main.c` — Test harness entry point

Both were being compiled and linked, causing linker error:
```
multiple definition of `main'
```

Additionally, the test compilation code (`audio_driver_tests.c`) was in the `test/` directory and not being included in the build.

## Solution Applied

### 1. **Conditional Compilation Guards** (Primary Fix)

**src/main.c** — Wrap original main() with guard:
```c
/* Line 46-71 */
#ifndef AUDIO_TESTS_MAIN
int main(void)
{
    /* ... FSM initialization ... */
}
#endif
```
- When `-DAUDIO_TESTS_MAIN` is defined, this main() is excluded
- When testing, audio_test_main.c provides the test harness main()

**src/audio_test_main.c** — Simplified test entry point:
```c
#ifdef AUDIO_TESTS_MAIN

#include <stdio.h>
#include "stm32l4xx_hal.h"
#include "audio_driver_tests.h"
#include "debug_uart.h"

extern void SystemClock_Config(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();           /* Reuse from main.c */
    DebugUart_Init();
    run_all_audio_tests();
    while (1) { HAL_Delay(1000); }
}

#endif
```
- Only defines main() when `AUDIO_TESTS_MAIN` is set
- Reuses SystemClock_Config() from main.c via extern
- Calls run_all_audio_tests() from audio_driver_tests.c

### 2. **Test Code Compilation** (Secondary Fix)

**src/audio_driver_tests.c** — Copied to src/ directory:
- Moved from `test/audio_driver_tests.c` to `src/audio_driver_tests.c`
- Now automatically included in build due to PlatformIO src pattern
- Implements run_all_audio_tests() function used by audio_test_main.c

### 3. **PlatformIO Configuration** (Already Correct)

**platformio.ini** — Environment [env:nucleo_l476rg_audio_test]:
```ini
[env:nucleo_l476rg_audio_test]
platform = ststm32
board = nucleo_l476rg
framework = stm32cube
debug_tool = stlink

build_flags =
    -D USE_HAL_DRIVER
    -D AUDIO_TESTS_MAIN      ← Enables test main(), disables FSM main()
    -O2
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Itest

build_unflags =
    -Os

build_src_filter =
    +<*>
    -<native_oled_tests_main.c>
    -<src/main.c>            ← Excluded when AUDIO_TESTS_MAIN is set
    -<src/fsm.c>             ← Not needed for audio tests
    -<src/stt.c>
    -<src/tts.c>
    -<src/uart_comm.c>
    -<src/uart_link.c>
    -<src/display/>

lib_extra_dirs =
    test
```

## Expected Build Result

When you run:
```bash
cd cosmo-stm32
python -m platformio run -e nucleo_l476rg_audio_test
```

The linker should now:
1. ✅ Compile audio_test_main.c (provides main)
2. ✅ Compile audio_driver_tests.c (provides run_all_audio_tests)
3. ✅ Exclude src/main.c (FSM main, disabled by guard)
4. ✅ Link successfully with NO "multiple definition of main" error

Expected output: **All 15 tests compile and link, ready to flash to Nucleo.**

## Build Verification Checklist

After running `pio run -e nucleo_l476rg_audio_test`, verify:

- [ ] **No compilation errors** — Especially no "multiple definition of `main'"
- [ ] **No undefined reference** to `run_all_audio_tests`
- [ ] **Build succeeds** with firmware.elf created in `.pio/build/nucleo_l476rg_audio_test/`
- [ ] **Firmware size reasonable** (~50-100 KB, not unusual for Nucleo with HAL)

## Next Steps After Successful Build

1. **Flash to Nucleo:**
   ```bash
   pio run -e nucleo_l476rg_audio_test -t upload
   ```

2. **Monitor UART output:**
   ```bash
   pio device monitor -e nucleo_l476rg_audio_test -b 115200
   ```

3. **Expected test output:**
   ```
   ╔═══════════════════════════════════════════╗
   ║  Audio Driver Test Suite                  ║
   ╚═══════════════════════════════════════════╝
   
   Phase 0-1: Initialization
   [PASS] audio_init() completes
   [PASS] Playback initial state (DONE)
   [PASS] Capture initial state (INACTIVE)
   [PASS] get_buffer() returns non-NULL
   [PASS] Buffer size > 0
   
   ... (11 more tests) ...
   
   ===================================================
    Audio Driver Test Summary
   ===================================================
    Total:   15
    Passed:  15
    Failed:  0
    Skipped: 0
   ===================================================
    ALL TESTS PASSED
   ```

## Files Modified

| File | Changes | Reason |
|------|---------|--------|
| `src/main.c` | Wrapped main() with `#ifndef AUDIO_TESTS_MAIN` | Prevent duplicate main definition |
| `src/audio_test_main.c` | Added extern SystemClock_Config(), simplified init | Reuse FSM clock setup, remove dependency |
| `src/audio_driver_tests.c` | Created (copied from test/) | Enable build inclusion |

## Rollback (If Needed)

To revert to FSM build (not audio tests):
```bash
pio run -e nucleo_l476rg -t upload    # Uses default environment, FSM runs normally
```
The `#ifndef` guard in main.c ensures FSM main() is only excluded when testing.
