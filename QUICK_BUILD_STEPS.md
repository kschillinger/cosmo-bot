# Quick Build Steps — Audio Driver Tests

## 🚀 Build & Flash

```bash
cd cosmo-stm32

# Build the test environment
python -m platformio run -e nucleo_l476rg_audio_test

# Flash to board
python -m platformio run -e nucleo_l476rg_audio_test -t upload

# Monitor UART output (115200 baud)
python -m platformio device monitor -e nucleo_l476rg_audio_test -b 115200
```

## ✅ What Was Fixed

| Issue | Solution |
|-------|----------|
| "multiple definition of `main'" error | Added `#ifndef AUDIO_TESTS_MAIN` guard to main.c |
| main.c and audio_test_main.c both compiling | Using conditional compilation flags |
| audio_driver_tests.c not found in build | Moved to src/ directory |
| SystemClock_Config() undefined | Added extern declaration, reuses from main.c |

## 📋 Expected Test Output

After flash and connect to serial:

```
╔═══════════════════════════════════════════════════════════╗
║         Audio Driver Test Suite (Bring-up Guide)          ║
╚═══════════════════════════════════════════════════════════╝

Phase 0-1: Initialization
[PASS] audio_init() completes
[PASS] Playback initial state (DONE)
[PASS] Capture initial state (INACTIVE)
[PASS] get_buffer() returns non-NULL
[PASS] Buffer size > 0

Phase 2: Playback API
[PASS] audio_play_buffer() basic call
[PASS] Playback state transitions

Phase 3: Capture API
[PASS] audio_capture_start/stop
[PASS] Capture auto-completes on timeout
[PASS] get_samples() returns valid pointer

Phase 6: FSM Integration
[PASS] FSM state sequence (IDLE→PLAY→DONE)

===================================================
 Audio Driver Test Summary
===================================================
 Total:   15
 Passed:  15
 Failed:  0
 Skipped: 0
===================================================
 ALL TESTS PASSED
===================================================

Test suite complete. Press reset to run again.
```

## 🔄 Switch Between Modes

**Run audio tests:**
```bash
pio run -e nucleo_l476rg_audio_test -t upload
```

**Run FSM normally (no tests):**
```bash
pio run -e nucleo_l476rg -t upload
```

Both configurations coexist without conflict — conditional compilation determines which main() is used.

## 📂 Key Files

```
cosmo-stm32/
├── src/
│   ├── main.c                    ← FSM main (disabled during tests)
│   ├── audio_test_main.c         ← Test harness main
│   ├── audio_driver_tests.c      ← 15 test cases
│   └── audio.{c,h}               ← Audio driver stubs
├── platformio.ini                ← [env:nucleo_l476rg_audio_test] defined
└── test/
    ├── audio_driver_tests.h      ← Test declarations
    └── README_AUDIO_TESTING.md   ← Testing guide
```

## 🛠️ Troubleshooting

**Still getting "multiple definition of main"?**
- Check that `-DAUDIO_TESTS_MAIN` is in build_flags for the nucleo_l476rg_audio_test environment
- Verify `#ifndef AUDIO_TESTS_MAIN` is present in main.c (line 47)
- Verify `#ifdef AUDIO_TESTS_MAIN` is present in audio_test_main.c (line 8)

**Undefined reference to `run_all_audio_tests`?**
- Confirm audio_driver_tests.c exists in src/ directory (not just test/)
- Verify audio_driver_tests.h is in test/ or include path
- Check `-Itest` is in build_flags

**Build hangs?**
- PlatformIO on first run downloads frameworks (~30-60 sec) — wait for completion
- On subsequent builds should be much faster (~10 sec)

---

**See also:**
- [BUILD_FIX_SUMMARY.md](BUILD_FIX_SUMMARY.md) — Detailed technical explanation
- [AUDIO_TESTING_QUICKSTART.md](AUDIO_TESTING_QUICKSTART.md) — Full testing workflow
- [INDEX.md](INDEX.md) — Complete documentation index
