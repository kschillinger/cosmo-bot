# Audio Driver Testing Suite — Quick Start

## What Was Created

You now have a **complete automated test framework + manual bring-up guide** for the audio driver. This is organized in two complementary tracks:

### 1. Automated Tests (Firmware-only)
- **File:** `test/audio_driver_tests.c`
- **Header:** `test/audio_driver_tests.h`
- **Main entry:** `src/audio_test_main.c`
- **PlatformIO config:** Updated `platformio.ini` with `nucleo_l476rg_audio_test` environment

**What it tests:**
- ✓ API contract (init, playback, capture)
- ✓ State machines (IDLE → PLAY → DONE)
- ✓ Buffer lifecycle (no-copy semantics)
- ✓ Concurrent operations (capture + playback)
- ✓ FSM integration points

### 2. Manual Hardware Bring-Up
- **Detailed guide:** Root docs (already provided)
- **Checklist:** `test/CHECKLIST_AUDIO_BRINGUP.md`
- **Testing strategy:** `test/README_AUDIO_TESTING.md`

---

## Quick Start

### Run Automated Tests (No Hardware Required)

```bash
cd cosmo-stm32

# Build and flash test suite
pio run -e nucleo_l476rg_audio_test -t upload

# Watch UART output
pio device monitor
```

**Expected output:**
```
╔═══════════════════════════════════════════════════════════╗
║         Audio Driver Test Suite (Bring-up Guide)          ║
╚═══════════════════════════════════════════════════════════╝

Phase 0-1: Initialization
───────────────────────────────────────────────────────────
[✓ PASS] audio_init() completes
[✓ PASS] Playback initial state (DONE)
[✓ PASS] Capture initial state (INACTIVE)
...

═══════════════════════════════════════════════════════════
 Audio Driver Test Summary
───────────────────────────────────────────────────────────
 Total:   18
 Passed:  15
 Failed:  0
 Skipped: 3
═══════════════════════════════════════════════════════════
 ✓ ALL TESTS PASSED
═══════════════════════════════════════════════════════════
```

### Manual Hardware Bring-Up

Once automated tests pass:

1. **Open:** `test/CHECKLIST_AUDIO_BRINGUP.md`
2. **Follow Phase 0 → Phase 7** in order
3. **Each phase has a STOP-gate** — do not skip ahead
4. **Equipment needed:** Multimeter, oscilloscope, speaker

---

## File Structure

```
cosmo-stm32/
├── platformio.ini                          (UPDATED: added audio test env)
├── src/
│   ├── audio.c                            (Stub driver)
│   ├── audio.h                            (API contract)
│   └── audio_test_main.c                  (Test entry point — NEW)
└── test/
    ├── audio_driver_tests.c               (Core test suite — NEW)
    ├── audio_driver_tests.h               (Test header — NEW)
    ├── README_AUDIO_TESTING.md            (Testing strategy — NEW)
    └── CHECKLIST_AUDIO_BRINGUP.md         (Manual bring-up — NEW)
```

---

## Test Coverage

| Aspect | Automated | Manual | Status |
|--------|-----------|--------|--------|
| Initialization | ✓ | Phase 0-1 | Pre-flight + power-on |
| Playback API | ✓ | Phase 2 | State transitions tested |
| Capture API | ✓ | Phase 3 | Start/stop/timeout |
| Concurrent ops | ✓ | Phase 4 | Capture + playback |
| Buffer semantics | ✓ | Phase 5 | No-copy contract validated |
| FSM integration | ✓ | Phase 6-7 | State sequence verified |
| DAC waveform | — | Phase 3 | Requires oscilloscope |
| Sample rate | — | Phase 4 | Frequency check (1 kHz ±5 Hz) |
| Amp output | — | Phase 5 | Scope on OUT+/OUT- |
| Speaker sound | — | Phase 5 | Ear test + volume sweep |

**Legend:**
- ✓ = Automated test validates this
- — = Manual hardware test validates this (oscilloscope/multimeter/ear)

---

## How to Use Each Document

### 1. `README_AUDIO_TESTING.md` — Testing Strategy
**When:** Before starting tests
**Purpose:** Understand the overall approach, map automated tests to hardware phases
**Read:** Top to bottom for context

### 2. `CHECKLIST_AUDIO_BRINGUP.md` — Manual Bring-Up Checklist
**When:** During hardware bring-up (Phases 0-7)
**Purpose:** Step-by-step instructions, STOP-gates, troubleshooting
**Use:** Print and check off each box as you proceed

### 3. `audio_driver_tests.c` — Core Test Implementation
**When:** To understand which API behaviors are tested
**Purpose:** Detailed test code with inline documentation
**Reference:** Check a specific test for edge cases it validates

### 4. Root bring-up guide (provided separately)
**When:** For in-depth hardware understanding
**Purpose:** Detailed explanation of each component, blocker list, decision tree
**Reference:** Deep dives on Why things work, not just How

---

## Testing Workflow

```
┌──────────────────────────────────────┐
│ 1. Automated Tests (No Hardware)     │
├──────────────────────────────────────┤
│ pio run -e nucleo_l476rg_audio_test  │
│ Expected: 15 PASS, 0 FAIL, 3 SKIP    │
└──────────┬───────────────────────────┘
           │
           ▼
┌──────────────────────────────────────┐
│ 2. Manual Bring-Up (Hardware)        │
├──────────────────────────────────────┤
│ Phase 0: Continuity (multimeter)     │
│ Phase 1: Power-on check              │
│ Phase 2: PB2 voltage (3.3 V)         │
│ Phase 3: DAC waveform (scope)        │
│ Phase 4: Sample rate (1 kHz ±5 Hz)   │
│ Phase 5: Amp output + ear test       │
│ Phase 6: API smoke test              │
│ Phase 7: FSM integration             │
└──────────┬───────────────────────────┘
           │
           ▼
┌──────────────────────────────────────┐
│ ✓ Audio Driver Ready for Production  │
└──────────────────────────────────────┘
```

---

## Key Testing Principles

### 1. **Automated Tests Validate API Contract**
Tests confirm the driver's state machine and API behavior without requiring hardware. If these pass, the software side is solid.

### 2. **Manual Tests Validate Hardware Chain**
Multimeter/scope checks confirm the physical chain: DAC → DMA → Amp → Speaker. If automated tests pass but hardware tests fail, the issue is in the hardware setup or HAL configuration.

### 3. **STOP-Gates Prevent Cascading Failures**
Each phase has a single critical gate. If it fails, the next phase **will** fail too. Don't skip ahead.

**Example:** If Phase 2 (PB2 = 3.3 V) fails, Phase 3 (DAC waveform) will show flat 0 V because the amp is still muted.

### 4. **No-Copy Buffer Contract**
The driver **does NOT copy buffers**. Caller must ensure buffer lifetime extends until `audio_playback_is_done()` returns 1. Automated tests validate this contract; manual tests would catch misuse (garbage audio mid-clip).

---

## Next Steps

1. **Run automated tests first:**
   ```bash
   pio run -e nucleo_l476rg_audio_test -t upload
   pio device monitor
   ```

2. **If all tests PASS:**
   - Open `test/CHECKLIST_AUDIO_BRINGUP.md`
   - Follow Phase 0-7 in order
   - Mark each STOP-gate as you pass it

3. **If any test FAILS:**
   - Check the error message in the test output
   - Verify `audio.c` or `audio.h` match what tests expect
   - Debugger tip: Set breakpoint in failing test, step through

4. **For real hardware driver (future):**
   - Replace `audio.c` stub with HAL calls
   - Run automated tests again (state machine still validated)
   - Proceed with manual bring-up phases
   - Use `printf()` breadcrumbs (see README) to debug hardware issues

---

## Troubleshooting

### Q: "All automated tests PASS but I get no sound"
**A:** Proceed to manual Phase 2 (PB2 voltage check). If PB2 ≠ 3.3 V, the driver didn't initialize (hard-fault or wrong firmware). Attach debugger.

### Q: "PA4 is flat 1.65 V during playback (no sine wave)"
**A:** This is the CubeMX IRQ conflict (blocker #8). Delete the `DMA2_Channel4_IRQHandler` from `stm32l4xx_it.c` — CubeMX's version is winning.

### Q: "Scope shows 125 Hz instead of 1 kHz"
**A:** SystemClock is 4 MHz, not 80 MHz. Check `SystemClock_Config()` actually called and switched to PLL.

### Q: "Amp is hot at idle"
**A:** VCC/GND miswired. Unplug immediately, recheck Phase 0 continuity.

---

## Files Modified

- **`platformio.ini`** — Added `[env:nucleo_l476rg_audio_test]` environment

## Files Created

- **`test/audio_driver_tests.c`** — Core test suite (18 tests, 7 phases)
- **`test/audio_driver_tests.h`** — Test function declarations
- **`src/audio_test_main.c`** — Test harness entry point
- **`test/README_AUDIO_TESTING.md`** — Detailed testing strategy
- **`test/CHECKLIST_AUDIO_BRINGUP.md`** — Printable bring-up checklist

---

## Resources

- **Automated tests:** Use for rapid iteration and CI/CD
- **Checklist:** Print and stick on monitor during bring-up
- **Root bring-up guide:** Reference for detailed explanations
- **README_AUDIO_TESTING.md:** Integration point for both tracks

---

## Questions?

Refer to:
1. **Decision tree** (bottom of root bring-up guide) — "I get no output at all"
2. **Test code comments** in `audio_driver_tests.c` — Why each test exists
3. **Checklist troubleshooting section** — Common issues + fixes

Good luck with the bring-up! 🎵
