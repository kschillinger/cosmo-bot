# Audio Driver Testing Strategy

This document ties together the **automated test suite** (`audio_driver_tests.c`) with the **hardware bring-up guide** provided in the root docs.

## Overview

The testing is split into **two tracks:**

1. **Automated (firmware-only)** — `audio_driver_tests.c`
   - Validates API contract and state machines
   - Runs on any platform (host, simulator, or target)
   - No external hardware required
   - Good for CI/CD and rapid iteration

2. **Hardware validation (manual + instrumentation)** — Bring-up guide phases
   - Validates DAC, DMA, amplifier, speaker
   - Requires multimeter, oscilloscope
   - Physical check after automated tests pass

---

## Test Execution Flow

### Automated Tests (No Hardware)

```bash
cd cosmo-stm32
pio run -e nucleo_l476rg_audio_test -t upload
pio device monitor
```

The test suite will print:
- Phase-by-phase test results
- PASS/FAIL status for each gate
- Summary of passed/failed/skipped tests

**Expected output (stub driver):**
```
╔═══════════════════════════════════════════════════════════╗
║         Audio Driver Test Suite (Bring-up Guide)          ║
╚═══════════════════════════════════════════════════════════╝

Phase 0-1: Initialization
───────────────────────────────────────────────────────────
[✓ PASS] audio_init() completes
[✓ PASS] Playback initial state (DONE)
[✓ PASS] Capture initial state (INACTIVE)
[✓ PASS] get_buffer() returns non-NULL
[✓ PASS] Buffer size > 0

Phase 2: Playback API
───────────────────────────────────────────────────────────
[✓ PASS] audio_play_buffer() basic call
[✓ PASS] Playback state transitions (IDLE→ACTIVE→DONE)
[✓ PASS] Sequential buffer playback

... (more phases)

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

### Hardware Validation (Physical Testing)

Once automated tests pass, proceed with the manual bring-up guide phases:

| Phase | Test | Instrument | STOP-Gate |
|-------|------|------------|-----------|
| **0** | Continuity + voltage | Multimeter | All traces checked, no shorts |
| **1** | Power-on, no firmware | — | Board enumerates, no smoke |
| **2** | Flash demo, PB2 check | Multimeter | **PB2 = 3.3 V** |
| **3** | DAC waveform | Oscilloscope on PA4 | **1 kHz sinusoid @ 1.65 V** |
| **4** | Sample rate | Scope frequency measure | **1000 Hz ± 5 Hz** |
| **5** | Amp output + speaker | Scope on OUT+/OUT-, ear | **Clean tone, volume scales** |
| **6** | API smoke test | UART monitor | Volume/mute/stop work |
| **7** | FSM integration | Display + UART | TTS clips play without glitch |

---

## Mapping: Test Suite → Bring-up Phases

### Phase 0-1: Initialization

**Automated tests:**
- `test_audio_init_completes()` — No hang
- `test_playback_initial_state()` — Starts in IDLE
- `test_buffer_get_returns_valid_pointer()` — No NULL buffers

**Hardware check (Phase 2 STOP-gate):**
```
pio device monitor    # Watch for [AUDIO] messages
# Multimeter on PB2 → should read 3.3 V after init completes
```

### Phase 2: Playback API

**Automated tests:**
- `test_play_buffer_basic()` — Accepts buffers
- `test_playback_state_transitions()` — IDLE → PLAY → DONE
- `test_playback_multiple_buffers_sequential()` — Back-to-back clips

**Hardware check (Phase 3 STOP-gate):**
```
# Scope probe on PA4
# Should see:
#   - Idle: ~1.65 V DC (flat)
#   - Playback: smooth 1 kHz sinusoid, ±1.3 Vpp around 1.65 V
```

### Phase 3-4: DAC + Sample Rate

**Automated tests:**
- State transitions (implicitly tested in playback)

**Hardware check (Phase 4 STOP-gate):**
```
# Scope on PA4, measure frequency with FFT or cursor
# Expected: 1000 Hz ± 5 Hz
#
# If frequency is wrong:
#   125 Hz   → SystemCoreClock is 4 MHz, not 80 MHz
#   Other    → TIM6 period (ARR) is wrong
```

### Phase 5: Amplifier + Speaker

**Automated tests:**
- No direct test (requires hardware DMA/amp)
- `test_api_for_fsm_integration()` — API contract validated

**Hardware check (Phase 5 STOP-gate):**
```
# Scope on amp output (OUT+ to OUT-)
# Expected at 100% volume: ~5-8 Vpp (PAM8302 rail-to-rail)
#
# Ear test:
#   - 4 volume levels (25/50/75/100%) should be audible
#   - No clicks/pops at transitions
#   - No hum or buzz (unless power supply issue)
```

### Phase 6: Driver API

**Automated tests:**
- All phases up to this point are prerequisite

**Real hardware:**
Add these to `main()` to exercise the API:
```c
audio_set_volume(25);     /* During playback */
audio_mute();
audio_unmute();
audio_playback_stop();    /* Mid-clip */
audio_play_buffer(...);   /* Restart after stop */
```

### Phase 7: FSM Integration

**Automated test:**
- `test_api_for_fsm_integration()` — FSM state sequence

**Real hardware:**
```
# Test the actual FSM flow:
#   FSM_STATE_IDLE
#     ↓
#   [Press B1 to trigger] → USER_INPUT sent to ESP32
#     ↓
#   FSM_STATE_PROCESSING_TTS (wait for TTS response)
#     ↓
#   audio_play_buffer(tts_clip)
#     ↓
#   FSM_STATE_PLAYBACK (spin on audio_playback_is_done())
#     ↓
#   audio_playback_is_done() returns 1
#     ↓
#   FSM_STATE_IDLE (display animation stops, ready for next input)
```

---

## CI/CD Integration

### PlatformIO Configuration

Add this environment to `platformio.ini`:

```ini
[env:nucleo_l476rg_audio_test]
platform = ststm32
board = nucleo_l476rg
framework = stm32cube
build_flags =
    -DAUDIO_TESTS_MAIN
    ${env.build_flags}
build_src_filter =
    +<*>
    -<.git/> -<.pio/> -<docs/> -<examples/>
lib_deps = ${env.lib_deps}
upload_protocol = stlink
monitor_speed = 115200
```

### CI Script Example

```bash
#!/bin/bash
# ci-test-audio.sh

cd cosmo-stm32

# Run automated tests
echo "Running audio driver automated tests..."
pio run -e nucleo_l476rg_audio_test -t upload

# Capture UART output
pio device monitor --quiet --exit-char '✓' --exit-char '✗' > test-output.log 2>&1 &
MONITOR_PID=$!

# Wait for tests to complete
sleep 30

kill $MONITOR_PID 2>/dev/null || true

# Parse results
if grep -q "✓ ALL TESTS PASSED" test-output.log; then
    echo "✓ Audio tests PASSED"
    exit 0
else
    echo "✗ Audio tests FAILED"
    cat test-output.log
    exit 1
fi
```

---

## Debugging Failed Tests

### Test Fails: `test_playback_state_transitions()`

**Likely cause:** Stub timer not working or `system_get_tick_ms()` returning stale values.

**Debug:**
```c
/* Add to test */
uint32_t t0 = system_get_tick_ms();
HAL_Delay(100);
uint32_t t1 = system_get_tick_ms();
printf("Elapsed: %lu ms (expected ~100)\r\n", t1 - t0);
```

### Test Fails: `test_capture_completes_on_timeout()`

**Likely cause:** Capture timeout value is too short or system clock not initialized.

**Debug:**
```c
printf("Capture started at: %lu ms\r\n", system_get_tick_ms());
while (audio_capture_is_active()) {
    printf("  Still active at: %lu ms\r\n", system_get_tick_ms());
    HAL_Delay(100);
}
```

### Hardware: No sound at all?

Follow the **decision tree** in the bring-up guide at the bottom. Specifically:

1. **PB2 reads 3.3 V?** → If NO, audio_init() didn't finish (check debugger)
2. **PA4 idle ~1.65 V?** → If NO, DAC not configured
3. **PA4 sinusoid during tone?** → If NO, DMA not refilling
4. **Sinusoid is 1 kHz?** → If NO, SystemClock or TIM6 is wrong
5. **Signal on amp input?** → If NO, broken jumper or DC-blocking cap issue
6. **Signal on amp output?** → If NO, amp not powered or SD pin low
7. **Speaker resistance ~8 Ω?** → If NO, speaker is dead

---

## Real Audio Driver Bring-up (Future)

When implementing the actual audio driver (DAC, DMA, PAM8302):

1. **Stub → HAL integration:** Replace `audio.c` stub with real HAL calls
2. **Run automated tests first** to validate state machine and API contract
3. **Then proceed with hardware phases** (multimeter/scope checks)
4. **CI/CD:** Automated tests catch regressions; hardware tests catch silicon issues

The automated test suite will then:
- Verify DAC and DMA are live (by checking for state transitions)
- Validate ISR is running (via DMA CNDTR countdown in debug output)
- Ensure no hangs or hard-faults during playback

---

## Test Coverage Summary

| Aspect | Coverage | Method |
|--------|----------|--------|
| **Initialization** | ✓ | API calls complete without error |
| **State machine** | ✓ | IDLE → PLAY → DONE transitions verified |
| **Playback API** | ✓ | Multiple buffers, sequential playback |
| **Capture API** | ✓ | Start/stop, timeout, sample access |
| **Concurrency** | ✓ | Capture + playback simultaneous |
| **No-copy semantics** | ✓ | Buffer lifetime, stack trap (doc) |
| **FSM integration** | ✓ | Complete state sequence |
| **DAC waveform** | ✗ | Requires oscilloscope (Phase 3) |
| **Sample rate** | ✗ | Requires oscilloscope (Phase 4) |
| **Amplifier output** | ✗ | Requires oscilloscope (Phase 5) |
| **Speaker sound** | ✗ | Requires ear + 30 cm distance (Phase 5) |

Automated tests reach the hardware boundaries; manual tests verify the real-world chain.

---

## Next Steps

1. **Add test environment to `platformio.ini`** (see CI/CD section above)
2. **Run: `pio run -e nucleo_l476rg_audio_test -t upload`**
3. **Watch UART output** for pass/fail results
4. **For hardware validation:** Proceed with manual bring-up phases from the root docs
