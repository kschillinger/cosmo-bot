# Audio Driver Bring-Up Checklist

Complete this checklist while following the detailed bring-up guide.
Each phase has a **STOP-gate** — do not proceed until it passes.

---

## Phase 0: Pre-flight (No Power, No Code)

**Wiring continuity** (multimeter in continuity mode):
- [ ] STM32 PA4 → PAM8302 `A+` or `IN+` (audio input)
- [ ] STM32 PB2 → PAM8302 `SD`/`SHDN` (shutdown/enable)
- [ ] STM32 GND → PAM8302 GND → speaker GND (star point)
- [ ] 3.3 V rail → PAM8302 VCC
- [ ] PAM8302 `OUT+` / `OUT-` → speaker leads

**Components:**
- [ ] 100 nF ceramic bypass cap across PAM8302 VCC↔GND (close to chip)
- [ ] (Optional) 100 nF DC-blocking cap in series on PA4 → amp input

**Speaker test:**
- [ ] Speaker leads: multimeter across speaker → reads ~**8 Ω** (not OL)
- [ ] No visible damage, solder joints intact

**STOP-GATE:** ✓ All continuity checks pass, no shorts, speaker is 8 Ω

---

## Phase 1: Power-On, No Firmware Change

- [ ] Power board (USB → Nucleo)
- [ ] Board enumerates over USB (ST-LINK visible on host)
- [ ] Nucleo PWR LED solid green
- [ ] **No smoke, no smell**, PAM8302 cool to touch (feel with finger)
- [ ] Existing firmware boots (you'll see UART messages if monitoring)

**STOP-GATE:** ✓ Board powered, no thermal issues, ST-LINK detected

---

## Phase 2: Flash Demo & Check PB2

Build and flash:
```bash
cd cosmo-stm32
pio run -e nucleo_l476rg_audio_demo -t upload
```

- [ ] Nucleo LD2 (user LED) blinks or matches expected behavior from `main()`
- [ ] UART shows `[AUDIO] audio_init() (stub)` or similar startup messages
- [ ] **Multimeter on PB2 reads 3.3 V** ← Critical gate!
  - If PB2 reads 0 V: audio_init() didn't finish. Attach debugger, set breakpoint in audio_init().

**STOP-GATE:** ✓ PB2 reads **3.3 V** (amp is enabled)

---

## Phase 3: DAC Waveform (Oscilloscope Required)

**Setup:**
- [ ] Oscilloscope probe on **PA4** (probe tip to PA4, probe ground to board GND)
- [ ] Scope set to: DC coupling, 1 V/div, 200 µs/div, auto-trigger

**Expected:**
- **Idle (between tone cycles):** Flat line at **~1.65 V** (DAC mid-scale)
- **During tone playback:** Smooth sinusoid
  - Frequency: **~1 kHz** ← You'll count ~2-3 cycles per screen at 200 µs/div
  - Amplitude: **~±1.3 Vpp** (1–2.3 V range, centered at 1.65 V)
  - Shape: Clean sine curve, no steps or noise

**If you see:**
- **Flat 0 V always** → DAC not enabled. Check `audio_init()` ran (PB2 = 3.3 V from Phase 2).
- **Flat 3.3 V always** → DAC output buffer disabled. Fix `audio_hal_dac_init()`.
- **Flat 1.65 V during tone** → DMA not running. Check for CubeMX IRQ-handler conflict (delete `DMA2_Channel4_IRQHandler` from `stm32l4xx_it.c`).
- **Stepped/blocky waveform** → Enable DAC output buffer (`DAC_OUTPUTBUFFER_ENABLE`).
- **Jittery/noisy sine** → Check power supply bypass caps, probe ground lead.

**STOP-GATE:** ✓ Clean **1 kHz sinusoid on PA4**, centered at **1.65 V**

---

## Phase 4: Sample Rate Verification (Scope)

**Measurement:**
- [ ] Scope frequency cursor or FFT on PA4 waveform
- [ ] Measure and record frequency: **_____ Hz**

**Expected:**
- [ ] **1000 Hz ± 5 Hz** ← Golden!
- [ ] If **125 Hz** → SystemClock is 4 MHz (MSI default), not 80 MHz. Fix `SystemClock_Config()`.
- [ ] If **other frequency** → TIM6 period wrong. Check `htim6.Init.Period = (SystemCoreClock / AUDIO_SAMPLE_RATE_HZ) - 1` should be **4999** at 80 MHz.

**Debug output** (add to `main()` for verification):
```c
printf("SystemCoreClock = %lu Hz\r\n", SystemCoreClock);   /* must be 80000000 */
printf("TIM6->ARR = %lu\r\n", TIM6->ARR);                  /* must be 4999 */
printf("DAC1->CR = 0x%08lx\r\n", DAC1->CR);               /* bit 0 = EN1 should be set */
```

**STOP-GATE:** ✓ Sinusoid frequency is **1000 Hz ± 5 Hz**

---

## Phase 5: Amplifier Output & Speaker (Scope + Ear)

### 5a. Amp Input Signal

- [ ] Scope probe on amp's audio input (after any DC-blocking cap)
- [ ] Expected: Same 1 kHz sinusoid as PA4, possibly DC-centered at 0 V
- [ ] Amplitude scales with software volume

### 5b. Amp Output Signal (Differential)

- [ ] Scope probe across **OUT+** and **OUT-** (differential measurement)
- [ ] Expected at **100% volume:** ~**5–8 Vpp** (PAM8302 rail-to-rail output)
- [ ] Expected at **25% volume:** ~**1.5–2 Vpp**
- [ ] Amplitude should scale linearly with volume setting

### 5c. Ear Test (Critical)

Hold speaker ~30 cm from ear. Demo cycles volume 25% → 50% → 75% → 100% with 0.5 s gaps.

- [ ] **All four levels are audibly different** ← Confirms amp working
- [ ] Each step is ~**6 dB louder** than previous
- [ ] **No clicks or pops** at volume transitions
- [ ] Tone is clean, no buzz
- [ ] **No 50/60 Hz hum** (unless ground loop)

**If you hear:**
- **Loud buzz that follows volume changes** → Power supply not bypassed near amp. Add 100 nF + 10 µF at amp VCC.
- **Constant hum regardless of volume** → Ground loop. Star-ground at amp's GND pin.
- **Distortion at 100% only** → Software clipping. Drop `AUDIO_DEFAULT_VOLUME` to 75%.
- **Distortion at all levels** → Amp over-driven on input. Check DC-blocking cap value.
- **Sound cuts in/out** → Underrun (CPU hogged >32 ms). Check `s_ctx.dma_underruns`.
- **"Chipmunk" pitch** → Sample rate wrong. Go back to Phase 4.

**STOP-GATE:** ✓ **Clean tone at all volumes, no noise or glitches**

---

## Phase 6: Driver API Smoke Test

Add this code to your `main()` after `audio_init()`:

```c
printf("\r\n=== Phase 6: API Smoke Test ===\r\n");

/* 1. Volume control */
printf("Test 1: Volume control (should hear volume drop mid-tone)\r\n");
audio_play_buffer(s_tone, DEMO_SAMPLES);
HAL_Delay(250);
audio_set_volume(25);
while (!audio_playback_is_done()) { HAL_Delay(10); }

/* 2. Mute / unmute */
printf("Test 2: Mute/unmute (should hear silence then sound)\r\n");
audio_play_buffer(s_tone, DEMO_SAMPLES);
HAL_Delay(250);
audio_mute();
HAL_Delay(250);
audio_unmute();
while (!audio_playback_is_done()) { HAL_Delay(10); }

/* 3. Mid-clip stop */
printf("Test 3: Stop mid-clip (should hear sudden silence, no pop)\r\n");
audio_play_buffer(s_tone, DEMO_SAMPLES);
HAL_Delay(300);
audio_playback_stop();

/* 4. Restart after stop */
printf("Test 4: Restart (should hear new tone)\r\n");
audio_play_buffer(s_tone, DEMO_SAMPLES);
while (!audio_playback_is_done()) { HAL_Delay(10); }

printf("=== Phase 6 Complete ===\r\n");
```

- [ ] Volume change mid-tone is audible and glitch-free
- [ ] Mute instantly silences, unmute restores to prior volume
- [ ] Stop produces silence within ~32 ms with **no click**
- [ ] Restart after stop works (IDLE→RUNNING path tested)

**STOP-GATE:** ✓ All four API calls work cleanly

---

## Phase 7: FSM Integration

Wire driver into state machine:
```
FSM_STATE_IDLE
  ↓ [User input or auto-trigger]
FSM_STATE_PROCESSING_TTS (generate or fetch TTS clip)
  ↓ [TTS complete]
audio_play_buffer(tts_clip)
  ↓
FSM_STATE_PLAYBACK (spin on audio_playback_is_done())
  ↓ [audio_playback_is_done() returns 1]
FSM_STATE_IDLE
```

- [ ] `audio_play_buffer()` returns success, not error
- [ ] FSM enters PLAYBACK state immediately after play call
- [ ] `audio_playback_is_done()` returns 1 within (clip_duration_ms + ~64 ms)
- [ ] FSM transitions back to IDLE cleanly
- [ ] Display animation runs concurrently (playback is non-blocking)

**TTS Buffer Lifetime** (Critical trap):
- [ ] TTS output buffer is NOT reused until `audio_playback_is_done()` returns 1
- [ ] Allocate two TTS buffers and ping-pong them, OR
- [ ] Require `audio_playback_is_done()` before starting new TTS

**STOP-GATE:** ✓ TTS clips play without glitch, FSM stays responsive

---

## Troubleshooting Decision Tree

```
                            No sound at all?
                                   │
              ┌────────────────────┴────────────────────┐
              │                                         │
     PB2 reads 3.3 V?                                 (start
              │                                       here)
        ┌─────┴─────┐
       YES         NO ─────► audio_init() didn't finish.
        │                    Hard fault or wrong firmware. Debugger.
        ▼
    PA4 idle ~1.65 V?
        │
    ┌───┴───┐
   YES     NO ─────► PA4 at 0 V or 3.3 V → DAC not configured.
    │                Check audio_hal_dac_init() output buffer flag.
    │
    ▼
  PA4 sinusoid during tone?
        │
    ┌───┴───┐
   YES     NO ─────► DMA not refilling. Almost always the CubeMX
    │                stm32l4xx_it.c IRQ conflict (blocker #8).
    │                Also check NVIC enable for DMA2_Channel4_IRQn.
    │
    ▼
  Sinusoid is 1 kHz?
        │
    ┌───┴───┐
   YES     NO ─────► Wrong clock. SystemClock_Config wasn't called
    │                or didn't switch to PLL@80MHz. Check
    │                SystemCoreClock value at runtime in debugger.
    │
    ▼
  Signal on amp input pin?
        │
    ┌───┴───┐
   YES     NO ─────► Broken jumper PA4 → amp, or DC-blocking cap
    │                shorted to ground.
    │
    ▼
  Signal on amp OUT+/OUT-?
        │
    ┌───┴───┐
   YES     NO ─────► Amp not powered (check VCC) OR SD pin still low
    │                (check PB2 reading — back to top of tree).
    │
    ▼
  Speaker continuity ~8 Ω?
        │
    ┌───┴───┐
   YES     NO ─────► Replace speaker / fix solder joint.
    │
    ▼
  IT'S WORKING NOW. (Or your ear's broken.)
```

---

## Phase Summary

| Phase | Test | STOP-Gate | Status |
|-------|------|-----------|--------|
| **0** | Continuity & voltage | All traces checked, no shorts | [ ] |
| **1** | Power-on | No smoke, ST-LINK detected | [ ] |
| **2** | PB2 voltage | **PB2 = 3.3 V** | [ ] |
| **3** | DAC waveform | **1 kHz sine @ 1.65 V** | [ ] |
| **4** | Sample rate | **1000 Hz ± 5 Hz** | [ ] |
| **5** | Amp & speaker | **Clean tone, all volumes** | [ ] |
| **6** | API smoke | **Volume/mute/stop work** | [ ] |
| **7** | FSM integration | **TTS clips play smoothly** | [ ] |

---

## Useful Debug Breadcrumbs

Add these to your `main()` for UART-printed status during bring-up:

```c
printf("SystemCoreClock = %lu Hz\r\n", SystemCoreClock);   /* must be 80000000 */
printf("TIM6->ARR = %lu\r\n", TIM6->ARR);                  /* must be 4999 */
printf("DAC1->CR = 0x%08lx\r\n", DAC1->CR);               /* bit 0 = EN1 */
printf("DMA2_Channel4->CNDTR = %lu\r\n", DMA2_Channel4->CNDTR);  /* counts down */
printf("PB2 (amp SD) = %d\r\n", HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_2)); /* 1=on */
printf("DMA underruns = %lu\r\n", s_ctx.dma_underruns);  /* 0 = healthy */
```

If `dma_underruns` ever goes non-zero, an ISR with higher priority than DMA2_Ch4 (priority 1) is hogging CPU for >32 ms. Likely culprits:
- SysTick locked at priority 0
- printf() inside another ISR
- Blocking SPI transaction on display from inside `HAL_GPIO_EXTI_Callback`

---

## Equipment Needed

- [ ] **Multimeter** (DC voltage, resistance, continuity)
- [ ] **Oscilloscope** (DC coupling, 1 V/div, 200 µs/div, frequency measure)
- [ ] **Nucleo-L476RG** board
- [ ] **PAM8302 breakout** (pre-soldered or on breadboard)
- [ ] **8 Ω speaker** (e.g., 0.5 W, 1 W common)
- [ ] **Jumper wires** (breadboard or direct-solder)
- [ ] **USB cable** (programming + power)

---

## Sign-Off

- [ ] All phases complete
- [ ] All STOP-gates passed
- [ ] Audio plays cleanly from FSM playback
- [ ] **Ready for production deployment**

**Date: ____________  Tester: ____________**

---

## Notes

(Space for recording issues, workarounds, or observations during bring-up)

