# Audio Driver Testing — Visual Workflow Guide

## The Complete Picture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     AUDIO DRIVER BRING-UP WORKFLOW                      │
└─────────────────────────────────────────────────────────────────────────┘

                              START HERE
                                  ↓
                    ┌─────────────────────────┐
                    │  Read QUICKSTART Guide  │ (5 min)
                    │  AUDIO_TESTING_         │
                    │  QUICKSTART.md          │
                    └────────────┬────────────┘
                                 ↓
           ┌─────────────────────────────────────────────┐
           │ AUTOMATED TESTS (No Hardware Required)      │
           ├─────────────────────────────────────────────┤
           │ pio run -e nucleo_l476rg_audio_test         │
           │ pio device monitor                          │
           │                                             │
           │ Expected: 15 PASS, 0 FAIL, 3 SKIP          │
           │ Runtime: ~3-4 seconds                       │
           └────────────┬────────────┬────────────────────┘
                        │            │
                    PASS│            │FAIL
                        │            │
              ┌─────────▼────────┐   │
              │ ✓ Tests PASS     │   │
              │ Proceed to       │   │
              │ Manual Bring-Up  │   │
              └────────┬─────────┘   │
                       │             │
                       │      ┌──────▼──────────────────┐
                       │      │ ✗ Debugging             │
                       │      │ - Check test comments   │
                       │      │ - See troubleshooting   │
                       │      │ - Re-run tests          │
                       │      └──────────────────────────┘
                       │
                       ▼
        ┌──────────────────────────────────────────────┐
        │  MANUAL BRING-UP (Hardware + Instruments)   │
        ├──────────────────────────────────────────────┤
        │  Print: CHECKLIST_AUDIO_BRINGUP.md          │
        │                                              │
        │  Phase 0: Continuity (Multimeter)           │ ← STOP-GATE ①
        │    ✓ PA4 → A+, PB2 → SD, GND → GND         │
        │    ✓ VCC powered, speaker ~8Ω              │
        │                                              │
        │  Phase 1: Power-On                          │
        │    ✓ USB enumerates, no smoke               │
        │                                              │
        │  Phase 2: PB2 Voltage                       │ ← STOP-GATE ②
        │    ✓ Multimeter on PB2 → 3.3 V             │
        │                                              │
        │  Phase 3: DAC Waveform (Oscilloscope)       │ ← STOP-GATE ③
        │    ✓ PA4 idle: flat at ~1.65 V            │
        │    ✓ PA4 tone: 1 kHz sine, ±1.3 Vpp       │
        │                                              │
        │  Phase 4: Sample Rate (Scope FFT)           │ ← STOP-GATE ④
        │    ✓ Frequency: 1000 Hz ± 5 Hz             │
        │                                              │
        │  Phase 5: Amp Output + Speaker (Ear Test)   │ ← STOP-GATE ⑤
        │    ✓ OUT+/OUT-: 5-8 Vpp at 100%            │
        │    ✓ Clean tone, volume sweep works         │
        │                                              │
        │  Phase 6: API Smoke Test                    │
        │    ✓ Volume/mute/stop work                  │
        │                                              │
        │  Phase 7: FSM Integration                   │ ← FINAL GATE ⑦
        │    ✓ TTS clips play through FSM             │
        │    ✓ No glitches, display responsive        │
        └──────────────────────────────────────────────┘
                       │
                       ▼
           ┌─────────────────────────────┐
           │ ✓✓✓ READY FOR DEPLOYMENT ✓✓✓│
           └─────────────────────────────┘
```

---

## Document Usage Map

```
                    ┌─ AUDIO_TESTING_QUICKSTART.md
                    │  └─ START HERE (5 min overview)
                    │
                    ├─ FILES_MANIFEST.md
                    │  └─ What files were created
                    │
                    ├─ IMPLEMENTATION_SUMMARY.md
                    │  └─ Comprehensive reference
                    │
                    └─ FULL TESTING WORKFLOW:
                       │
                       ├─ Automated Tests
                       │  ├─ cosmo-stm32/test/
                       │  │  ├─ audio_driver_tests.c (core implementation)
                       │  │  ├─ audio_driver_tests.h (API)
                       │  │  └─ README_AUDIO_TESTING.md (strategy)
                       │  │
                       │  └─ cosmo-stm32/src/
                       │     └─ audio_test_main.c (entry point)
                       │
                       └─ Manual Hardware Tests
                          └─ cosmo-stm32/test/
                             └─ CHECKLIST_AUDIO_BRINGUP.md (PRINT THIS!)
```

---

## Test Execution Paths

### Path 1: Quick Test (Minimal Hardware)
```
1. Connect USB to Nucleo (power + programming)
2. pio run -e nucleo_l476rg_audio_test -t upload
3. pio device monitor
4. ✓ See "ALL TESTS PASSED" in UART output
```
**Time:** ~2 minutes  
**Equipment:** USB cable only  
**Outcome:** Firmware validation ✓

### Path 2: Full Hardware Bring-Up (Production)
```
1. Run Path 1 (automated tests)
2. If PASS, proceed to manual hardware validation
3. Print CHECKLIST_AUDIO_BRINGUP.md
4. Gather: Multimeter, Oscilloscope, Speaker
5. Follow Phase 0-7 checklist
6. Check each STOP-GATE before advancing
7. ✓ Sign off when all phases complete
```
**Time:** ~2-4 hours  
**Equipment:** Multimeter, oscilloscope, Nucleo, speaker, jumpers  
**Outcome:** Full hardware validation ✓

### Path 3: CI/CD Pipeline
```
1. ./ci-audio-test.sh          # Full automated test
   ├─ Build with test flag
   ├─ Flash to board
   ├─ Capture UART output
   └─ Parse results
2. Exit code 0 = ✓ PASS, 1 = ✗ FAIL
3. Use for gated merges in git
```
**Time:** ~1 minute  
**Equipment:** Nucleo connected via USB  
**Outcome:** Regression testing ✓

---

## Key Decision Points

### Q: Which tests do I run?

```
Scenario 1: I just want to verify the API works
  → Run Path 1 (automated tests only)
  → Takes 2 minutes
  → No hardware needed

Scenario 2: I'm bringing up the hardware for the first time
  → Run Path 2 (full bring-up)
  → Takes 2-4 hours
  → Requires multimeter + scope

Scenario 3: I'm adding this to CI/CD
  → Use ci-audio-test.sh in Path 3
  → Runs automatically on each commit
  → Fails early if regression detected
```

### Q: Where do I start if tests fail?

```
Automated test fails?
  ↓
  Check test comments in audio_driver_tests.c
    ↓
    Yes, found the issue → Fix and re-run
    No, still confused → See README_AUDIO_TESTING.md

Hardware test fails? (Phase 2 PB2 check)
  ↓
  Use the decision tree in CHECKLIST_AUDIO_BRINGUP.md
    ↓
    Multimeter check → Continuity check → Software check → Fix
```

---

## STOP-Gates Checklist

These are **critical gates**. Do not proceed to the next phase if the current gate fails.

| Gate | Phase | Measurement | Expected | If Fail: Check |
|------|-------|-------------|----------|----------------|
| ① | 0 | Continuity | All traces pass | Solder joints |
| ② | 2 | PB2 voltage | 3.3 V | `audio_init()` ran |
| ③ | 3 | PA4 waveform | 1 kHz sine @ 1.65 V | DAC/DMA running |
| ④ | 4 | PA4 frequency | 1000 Hz ± 5 Hz | SystemClock, TIM6 |
| ⑤ | 5 | Amp output | 5-8 Vpp @ 100% | Amp powered, speaker alive |
| ⑥ | 6 | API functions | Volume/mute/stop work | ISR priorities |
| ⑦ | 7 | FSM playback | Clean TTS audio | Buffer lifetime |

---

## Equipment & Tools

### Multimeter
- **Phase 0:** Continuity mode (audio traces PA4→A+, PB2→SD, GND→GND)
- **Phase 0:** Resistance mode (speaker ~8Ω)
- **Phase 1:** DC voltage (PAM8302 VCC = 3.3 V)
- **Phase 2:** DC voltage (PB2 = 3.3 V)

### Oscilloscope
- **Phase 3:** DC coupling, 1 V/div, 200 µs/div, probe on PA4
  - Expect: 1 kHz sine wave, 1–2.3 V range
- **Phase 4:** Frequency measure (FFT or cursor)
  - Expect: 1000 Hz ± 5 Hz
- **Phase 5:** Probe on amp OUT+/OUT-
  - Expect: 5–8 Vpp at 100% volume

### Software
- **PlatformIO** (build system)
- **STM32Cube HAL** (already in repo)
- **Terminal/UART monitor** (115200 baud)

---

## Error Symptoms & Fixes

| Symptom | Phase | Likely Cause | Fix |
|---------|-------|--------------|-----|
| Test never completes | 1 | Hard-fault in `audio_init()` | Debugger: breakpoint in init |
| PA4 is flat 0 V | 3 | DAC not enabled | Check `audio_hal_dac_init()` |
| PA4 is flat 1.65 V during tone | 3 | DMA not running (CubeMX IRQ conflict) | Delete `DMA2_Channel4_IRQHandler` from `stm32l4xx_it.c` |
| PA4 is 125 Hz not 1 kHz | 4 | SystemClock is 4 MHz not 80 MHz | Fix `SystemClock_Config()`, call `HAL_RCC_GetSystemClockFreq()` |
| Amp is hot at idle | 1 | VCC/GND miswired | **UNPLUG NOW**, check Phase 0 |
| No sound, but scope shows signal | 5 | Speaker dead or disconnected | Measure speaker: should be ~8Ω |
| Sound cuts in/out | 5 | CPU starvation (underrun) | Check ISR priorities, remove printf() from ISRs |
| Volume doesn't change mid-tone | 6 | ISR not updating DAC | Check DMA reload mechanism |

---

## Success Indicators

### ✓ Automated Tests Pass
```
[✓ PASS] audio_init() completes
[✓ PASS] Playback state transitions (IDLE→ACTIVE→DONE)
[✓ PASS] Sequential buffer playback
... (15 total)

═══════════════════════════════════════════════════════════
 ✓ ALL TESTS PASSED
═══════════════════════════════════════════════════════════
```

### ✓ Phase 2 (PB2 Voltage)
```
Multimeter on PB2 reads: 3.3 V ✓
```

### ✓ Phase 3 (DAC Waveform)
```
Scope on PA4 shows:
  Idle: Flat line at ~1.65 V ✓
  Tone: Smooth 1 kHz sinusoid, 1–2.3 V range ✓
```

### ✓ Phase 5 (Speaker Sound)
```
Ear test at 30 cm distance:
  25% volume: audible ✓
  50% volume: ~6 dB louder than 25% ✓
  75% volume: ~6 dB louder than 50% ✓
  100% volume: ~6 dB louder than 75% ✓
  No clicks/pops/hum ✓
```

### ✓ Phase 7 (FSM Integration)
```
Press B1 to trigger TTS playback:
  ✓ FSM enters PLAYBACK state
  ✓ Audio plays (tone or TTS clip)
  ✓ FSM returns to IDLE when done
  ✓ Display animation runs concurrently
  ✓ No glitches on restart
```

---

## Sign-Off (For Lab Notebook)

```
Audio Driver Bring-Up Completion Checklist

Project: Cosmo Bot
Board: STM32L476RG Nucleo
Amp: PAM8302 (3.3 V module)
Speaker: 8 Ω

Automated Tests:       ☐ PASS  ☐ FAIL
Phase 0 (Continuity): ☐ PASS  ☐ FAIL  
Phase 1 (Power-On):   ☐ PASS  ☐ FAIL
Phase 2 (PB2=3.3V):   ☐ PASS  ☐ FAIL
Phase 3 (DAC sine):   ☐ PASS  ☐ FAIL
Phase 4 (1 kHz freq): ☐ PASS  ☐ FAIL
Phase 5 (Speaker):    ☐ PASS  ☐ FAIL
Phase 6 (API):        ☐ PASS  ☐ FAIL
Phase 7 (FSM):        ☐ PASS  ☐ FAIL

Status: ☐ READY FOR DEPLOYMENT

Date: ___________  Tester: _______________
```

---

## Additional Resources

- **Detailed bring-up guide** (root docs) — Why each component matters
- **inline test comments** — What each test validates
- **Decision tree** (CHECKLIST) — "I get no sound" troubleshooting
- **CI/CD script** (`ci-audio-test.sh`) — Automation for regression testing

---

**Ready to test?** → Start with `AUDIO_TESTING_QUICKSTART.md` (5 min read)  
**Need to debug?** → Check test comments in `audio_driver_tests.c`  
**Doing hardware bring-up?** → Print `CHECKLIST_AUDIO_BRINGUP.md`  
**Want all details?** → Read `IMPLEMENTATION_SUMMARY.md`

Good luck! 🎵
