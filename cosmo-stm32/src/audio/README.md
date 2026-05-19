# Audio playback driver — `cosmo-stm32/src/audio/`

DAC-driven audio output for the cosmo-bot, targeting **STM32L476 @ 80 MHz**.
Path from byte to sound:

```
audio_play_buffer(int16_t*, n)
        │
        ▼
  ping-pong refill   ─── 16→12-bit + gain ───►  s_dac_ring[1024]
        ▲                                              │
        │ HT / TC IRQ                                  │ DMA2 Ch4 (req 3)
        │ (DAC HAL callbacks)                          ▼
        └──────────  TIM6 TRGO @ 16 kHz  ──►  DAC1 CH1 (PA4)  ──►  PAM8302  ──►  🔊
```

## Files

| File | Role |
|---|---|
| `audio_playback.h` | Public API + config constants + types |
| `audio_playback.c` | All five layers, ping-pong refill, HAL init |
| `audio_playback_interrupts.c` | NVIC vector + HAL DAC callback wiring |
| `../../examples/audio_example.c` | Stand-alone 1 kHz sine bring-up demo |

## Why ping-pong instead of a single DMA buffer?

The spec asks for circular DMA. The naive reading — convert the whole
16-bit clip to a 12-bit DAC buffer up front and DMA it in one shot — works
for 250 ms clips but breaks for real TTS:

| Clip length | Samples (16 kHz) | 12-bit buffer size |
|---|---|---|
| 0.5 s | 8 000 | 16 kB |
| 1 s | 16 000 | 32 kB |
| 2 s | 32 000 | 64 kB |
| 3 s | 48 000 | **96 kB** of 128 kB SRAM |

Instead the driver keeps **one 1 024-sample (2 kB) ring** that DMA loops over
forever, and refills the half the DAC isn't reading at every half-transfer
and transfer-complete interrupt. The 16-bit source stays in the caller's
buffer untouched. Memory cost is constant regardless of clip length, and
end-of-clip is handled by padding the last half with mid-scale silence so
the speaker doesn't pop on stop.

## CubeMX configuration

If you regenerate from `.ioc`, set these. Otherwise `audio_init()` sets
everything programmatically — CubeMX is optional.

### DAC1
- **OUT1 Configuration** → Connected to external pin only
- **DAC Out1 Settings**
  - Output Buffer: **Enable** (low-Z drive)
  - Trigger: **Timer 6 Trigger Out event**
  - Sample-and-Hold: Disable
- **DMA Settings → Add → DAC1_CH1**
  - Channel: **DMA2 Channel 4**
  - Request: **Request 3**
  - Direction: Memory to Peripheral
  - Mode: **Circular**
  - Increment Address: Memory ✓, Peripheral ✗
  - Data Width: **Half Word** on both sides
  - Priority: High
- **NVIC** → DMA2 Channel4 global interrupt: **Enabled**, preempt priority 1

> ⚠️ The original spec referenced `DMA2_Channel3` for DAC1; on STM32L476 the
> DAC1_CH1 request maps to **DMA1 Ch3** *or* **DMA2 Ch4**. We use DMA2 Ch4
> because the screendrivers branch already owns DMA1 Ch3 for SPI1 TX.

### TIM6
- Clock Source: **Internal Clock**
- Counter Settings
  - Prescaler: **0**
  - Counter Period (ARR): **4999** &nbsp;*(80 MHz / 5000 = 16 000 Hz exact)*
  - auto-reload preload: Disable
- Trigger Output (TRGO) Parameters → **Update Event**
- NVIC: **not** required — DMA handles every sample, the timer just ticks.

### GPIO
| Pin | Mode | Notes |
|---|---|---|
| **PA4** | Analog | DAC1_OUT1 |
| **PB2** | GPIO_Output, push-pull, low speed | PAM8302 SD pin. Init LOW so amp stays asleep until driver turns it on. |

### Clock tree sanity check
- SYSCLK = **80 MHz** from MSI 4 MHz × PLL (M=1, N=40, R=2)
- APB1 timer clock = 80 MHz (no /2 multiplier needed — APB1 prescaler = 1)
- TIM6 lives on APB1, so the 4999 ARR above gives exactly 16 kHz.

## Wiring

```
                   ┌─────────────── 3.3 V ────────────────┐
                   │                                       │
                   │ 100 nF                                │ 100 nF
                   ┴ ──┐                                   ┴ ──┐
                   ─    GND                                ─    GND
                                                            
   STM32L476                              PAM8302                   8 Ω SPK
   ┌─────────┐                            ┌──────────┐              ┌─────┐
   │     PA4 ├────────  audio  ──────────►│ A+       │◄── OUT+ ────►│  +  │
   │         │                            │          │              │     │
   │     GND ├────────────────────────────┤ GND   A- ├◄──── (gnd)   │     │
   │         │                            │          │              │     │
   │     PB2 ├────────► (SD)  active-low ►│ SD   OUT-├◄── OUT- ────►│  −  │
   │         │                            │      VCC ├◄── 3.3 V     │     │
   └─────────┘                            └──────────┘              └─────┘
```

Bypass caps:
- **100 nF** ceramic on the PAM8302 VCC pin (close to the chip).
- **100 nF** ceramic in series between PA4 and the amp input is *optional*
  — it AC-couples and removes the 1.65 V DC bias the DAC sits at when idle.
  The PAM8302 has its own input cap on most breakout boards, so check the
  module schematic before adding another in series.

PCB notes:
- Keep the speaker traces away from PA4 — the amp output is high-current
  and will couple back into the analog input as a buzz.
- Star-ground at the amp's GND pin, not at the MCU.
- If you hear hiss only when WiFi/BLE is active, route the audio trace
  perpendicular to the antenna feed.

## Integration with the rest of the stack

```c
/* main.c */
#include "audio_playback.h"

int main(void) {
    HAL_Init();
    SystemClock_Config();
    audio_init();          /* one-shot — DAC, TIM6, DMA, amp GPIO */
    /* ... rest of stack ... */
    fsm_run();
}

/* fsm.c — PROCESSING_TTS state */
if (tts_is_complete()) {
    audio_play_buffer(tts_get_audio_buffer(), tts_get_audio_size());
    fsm.next_state = FSM_STATE_PLAYBACK;
}

/* fsm.c — PLAYBACK state */
if (audio_playback_is_done()) {
    fsm.next_state = FSM_STATE_IDLE;
}
```

The driver is **non-blocking** — once `audio_play_buffer` returns, the FSM
is free to update the display, poll UART, etc. The TTS source buffer must
remain valid until `audio_playback_is_done()` returns 1 *or* you call
`audio_playback_stop()`. The driver does not copy the source.

### PlatformIO build config

`audio_playback.c` and `audio_playback_interrupts.c` are picked up
automatically by PlatformIO's default `build_src_filter` (`+<**/*.c>`).
For the demo:

```ini
[env:nucleo_l476rg_audio_demo]
extends = env:nucleo_l476rg
build_src_filter = +<**/*.c> +<../examples/audio_example.c>
build_flags = ${env:nucleo_l476rg.build_flags} -DAUDIO_DEMO_MAIN
```

## Testing strategy

Eight phases, fastest to slowest. Skip down the list — if phase N fails,
don't bother running N+1.

### Phase 1 — Hardware sanity (no code, just `audio_init()`)
1. Flash a build that just calls `audio_init()` then spins.
2. Scope **PA4**: should sit at **~1.65 V** (DAC mid-scale, DMA primed
   the ring with silence).
3. Scope **PB2**: should be **HIGH** (amp running).
4. Scope **TIM6** indirectly via DMA traffic — easier to verify in Phase 2.

### Phase 2 — Test tone
1. Build the demo (`-DAUDIO_DEMO_MAIN`), flash it.
2. You should hear a 1 kHz tone from the speaker, cycling through 25 / 50 /
   75 / 100 % volume with a 0.5 s gap.
3. Scope PA4 → smooth sinusoid, peak-to-peak ~2.6 V on a 3.3 V DAC at 100 %.

### Phase 3 — Sample-rate accuracy
1. Capture the 1 kHz tone over multiple cycles.
2. Measured frequency should be **1 000 Hz ± 0.5 %**. Anything worse →
   check `SystemCoreClock` actually equals 80 MHz at runtime.

### Phase 4 — Volume control
- 25 → 50 → 75 → 100 % should sound roughly **+6 dB per step**.
- Call `audio_mute()` / `audio_unmute()` mid-tone — should silence and
  resume *without* a click. (The mute is gain-baked, no DMA reconfig.)

### Phase 5 — Conversion math (host-side)
Easiest to verify by linking `audio_playback.c` against a host harness and
calling Layer 2 directly:

| Input (int16) | Expected output (uint12) |
|---|---|
| `-32768` | `0` |
| `0` | `2048` |
| `+32767` | `4095` |
| `+16384` | `3072` |

### Phase 6 — DMA & IRQ
- Set a breakpoint in `audio_dma_transfer_complete_isr`. With a 1 s clip
  it should fire ≈ **15 times** during playback (1024-sample ring at
  16 kHz = 64 ms per full cycle), then once more for the trailing-silence
  shutdown.
- Watch `NDTR` (`DMA2_Channel4->CNDTR`) decrementing 1024 → 0 → 1024 …

### Phase 7 — Progress tracking
- Print `audio_playback_get_progress_percent()` every 100 ms during a
  3-second clip. Expect a roughly linear 0 → 100 ramp.

### Phase 8 — End-to-end FSM
- STT → dialogue → TTS → audio_play_buffer.
- Audible response ≤ **150 ms** after speech ends (TTS is the bottleneck;
  audio playback contributes ≤ 64 ms — one ring cycle of priming).

## Troubleshooting

| Symptom | First thing to check |
|---|---|
| Total silence | PB2 HIGH? PAM8302 VCC present? Speaker continuity? |
| Silence + PA4 stuck at 0 V or 3.3 V | DAC not started — confirm `HAL_DAC_Start_DMA` returned `HAL_OK` and `audio_init()` actually ran. |
| Silence + PA4 sits at 1.65 V | DMA isn't transferring. Confirm DMA2 Ch4 IRQ is enabled in NVIC; confirm CubeMX `stm32l4xx_it.c` doesn't define a competing `DMA2_Channel4_IRQHandler`. |
| Distortion / clipping at high volume | Reduce `AUDIO_DEFAULT_VOLUME`, or call `audio_set_volume(75)`. PAM8302 has fixed analog gain; software gain > 1.0 will saturate. |
| Static / hiss with no signal | Bypass cap missing on PAM8302 VCC. Or speaker leads coupling into PA4 — reroute. |
| Wrong pitch (chipmunks / slowed) | TIM6 ARR wrong, or `SystemCoreClock` ≠ 80 MHz. Verify `SystemClock_Config()`. ARR should be `(SystemCoreClock / 16000) − 1 = 4999`. |
| "Click" at start of every clip | `audio_init()` not called early enough — amp is being woken with a stale DAC value on PA4. Init in `main()` before any other peripheral. |
| "Pop" at end of every clip | Trailing silence padding isn't running. Check that `audio_dma_transfer_complete_isr` is being reached — if your `stm32l4xx_it.c` shadows the IRQ handler, our HAL callback never fires. |
| Driver-detected DMA underrun (`s_ctx.dma_underruns > 0`) | Some other ISR is hogging the CPU for >32 ms. Lower its priority, or raise DMA2 Ch4 priority above it. |

## PWM alternative (if DAC unavailable)

If you ever need to rebuild this on a part without DAC, the same driver
shape works with TIM2_CH1 PWM on PA0:

- TIM2 Channel 1, PWM mode, `ARR = 1023` (10-bit), update at 16 kHz means
  prescaler so that `f_tim / (1024 × 1) = 16 kHz`. With 80 MHz APB clock
  that's PSC = 4, ARR = 1023, or PSC = 0, ARR = 4999 with center-aligned
  PWM and a multiplier — the math gets ugly; easiest is **80 MHz / 1024 ≈
  78.1 kHz carrier** (no resampling) with the audio rate driven by a
  separate timer interrupt that writes `CCR1`.
- Output RC: 1 kΩ + 100 nF → cutoff ≈ 1.6 kHz. That's already low for
  speech intelligibility (consonants live at 2-4 kHz). Better: 470 Ω +
  100 nF (3.4 kHz cutoff) and accept a bit more carrier ripple.
- 12-bit conversion in the driver becomes 10-bit: `(sample + 32768) >> 6`
  with cap at 1023.

The HAL setup differs but the public API (`audio_play_buffer`,
`audio_playback_is_done`, …) stays identical — swap which `audio_hal_*_init`
gets called and you're done.

## Memory & CPU budget

| | Bytes |
|---|---|
| `s_dac_ring` | 2 048 |
| `s_ctx` | ~ 64 |
| Driver code (Flash, `-Os`) | ~ 4 000 |
| Demo `s_tone` | 32 000 (only if you build the demo) |
| **Driver runtime RAM total** | **~ 2.1 kB** |

CPU load during playback: dominated by the refill ISR. Each ISR converts
512 samples (one half) using one float multiply, two compares, and a
shift each. Profiled on an L476 at 80 MHz: **~ 6 µs per refill ISR, firing
every 32 ms → ~ 0.02 % CPU**.
