# OLED display driver — I2C variant — `cosmo-stm32/src/display/`

128x64 (default) monochrome OLED driver for the cosmo-bot, targeting
**STM32L476** at 80 MHz, **I2C1 @ 400 kHz**, horizontal addressing mode.
Compatible with **SSD1306** (default, 128x64). Support is also included for
**SH1106** (128x64) and **SH1107** (128x128) via build flags.

This is the I2C-compatible companion to the SPI driver on the
`screendrivers` branch.

| Layer | File                  | Contents                                              |
|------:|-----------------------|-------------------------------------------------------|
| 2     | `oled_display.c`      | I2C cmd/data, init, contrast, on/off                  |
| 3     | `oled_display.c`      | 1 KB framebuffer, full + region flush                 |
| 4     | `oled_display.c`      | Bresenham line, midpoint circle, rect, text, bitmap   |
| 5     | `oled_animations.c`   | State machine, idle/listening/processing/responding   |
|  font | `font_5x7.{h,c}`      | 96-glyph ASCII bitmap (480 B Flash)                   |

Standalone bring-up demo: `cosmo-stm32/examples/oled_example.c`.

---

## Wiring — 4-pin I2C OLED → Nucleo-L476RG

```
  OLED          STM32L476  (Nucleo Arduino I2C header)
  ────          ─────────
  VCC   ──────  3V3
  GND   ──────  GND
  SCL   ──────  PB8  (I2C1_SCL, AF4)   — Nucleo D15  [primary]
  SDA   ──────  PB9  (I2C1_SDA, AF4)   — Nucleo D14  [primary]
```

Most 4-pin OLED breakouts have onboard **4.7k–10k pull-ups** on SDA/SCL,
so no external pull-ups needed. If yours doesn't (check the back of the
board), add 4.7k from each line to 3V3.

A 100 nF + 1 µF decoupling pair across the module's VCC/GND, right at
the panel, is good practice.

If no ACK is found on PB8/PB9, the demo firmware automatically retries
the alternate I2C1 pin pair **PB6/PB7** (Nucleo D10/D9).

---

## I2C address

Most 0.96" SSD1306 modules ship at **7-bit address 0x3C** (0x78 in 8-bit
HAL form). A few have a solder jumper to switch to **0x3D** (0x7A);
check the back of the board. Override the default by setting
`cfg.address = (0x3D << 1)` before `oled_init()`.

---

## STM32CubeMX configuration

If you regenerate from CubeMX, configure:

**Clock tree.** HSI16 + PLLM=1, PLLN=10, PLLR=DIV2 → 80 MHz SYSCLK,
voltage scaling 1, Flash latency 4. (Same as the Phase-1 main.c.)

**I2C1.**
```
  Mode             : I2C
  Timing           : 0x10909CEC  (400 kHz @ 80 MHz APB1 clock)
                      ↳ CubeMX "Timing Configurator" → 400 kHz, FM
  Analog filter    : enabled
  Digital filter   : 0
  General call     : disabled
  Clock stretching : enabled
```

**GPIO.** PB8 and PB9 in `GPIO_MODE_AF_OD` (alternate function,
open-drain), alternate function **AF4** (I2C1), `GPIO_PULLUP` (cheap
insurance even if the module has pullups), high speed.

DMA is optional. The blocking path at 400 kHz takes ~26 ms per full
flush, well under the 100 ms / 10 Hz animation budget.

---

## API summary

```c
oled_config_t cfg = {
    .hi2c    = &hi2c1,
    .address = OLED_I2C_ADDR_8BIT,   /* 0x78; or (0x3D << 1) for 0x3D modules */
};
oled_init(&cfg);
oled_set_contrast(0xCF);

oled_framebuffer_clear();
oled_draw_string(8, 8, "Hello, world", OLED_COLOR_WHITE);
oled_draw_circle(64, 32, 20, /*filled=*/0, OLED_COLOR_WHITE);
oled_framebuffer_update_display();

/* Or use the high-level state machine: */
oled_display_set_state(DISPLAY_STATE_PROCESSING);
for (uint16_t f = 0; ; ++f) {
    oled_update_animation_frame(f);
    oled_framebuffer_update_display();
    HAL_Delay(100);   /* 10 Hz */
}
```

---

## Performance (estimated, 400 kHz I2C)

| Operation                           | Typical    |
|-------------------------------------|-----------:|
| `oled_init()` end-to-end            | ~55 ms     |
| Full framebuffer flush              | ~26 ms     |
| Region flush (e.g. waveform strip)  | ~3–8 ms    |
| `oled_framebuffer_clear()` (memset) | < 50 µs    |
| `oled_set_pixel()`                  | ~0.2 µs    |
| `oled_draw_char()`                  | ~70 µs     |
| Animation tick (draw + flush)       | ~28 ms     |

Memory:

| Region                              | Bytes |
|-------------------------------------|------:|
| Framebuffer (RAM, static)           |  1024 |
| Driver state                        |    16 |
| Font 5x7 (Flash, const)             |   480 |
| Code, `-Os`                         | ~5000 |

At 400 kHz the bus itself caps full-screen refresh at ~38 Hz. For 10 Hz
animations the budget is plenty; for faster, use region updates or
switch to Fast-mode Plus (1 MHz — SSD1306 supports it).

---

## Testing strategy

1. **Bus probe.** Pull up SDA/SCL, scope both lines: high (~3V3) idle.
   `HAL_I2C_IsDeviceReady(&hi2c1, 0x78, 3, 20)` should return `HAL_OK`.
   If not, address is probably 0x7A or wiring is off.
2. **Init.** Screen goes from off → briefly solid → blank framebuffer.
3. **Pattern fill.** `oled_framebuffer_fill()` then flush — uniform
   white, no missing rows.
4. **Per-pixel.** Light the four corners individually; check for
   off-by-one.
5. **Primitives.** Lines, rect outline+fill, circles.
6. **Text.** Print all 96 glyphs across the rows.
7. **Animation.** Cycle states at 10 Hz; verify smooth blinks and orbit.
8. **Integration.** Drive `oled_display_set_state()` from the ESP32
   over the existing UART link.

---

## Debugging — common issues

| Symptom                              | Likely cause                                                          |
|--------------------------------------|-----------------------------------------------------------------------|
| `HAL_I2C_IsDeviceReady` fails        | Wrong address (try 0x7A), pullups missing, SDA/SCL swapped            |
| No ACK on PB8/PB9                    | Try PB6/PB7 (D10/D9) — demo firmware now retries automatically        |
| Display stays fully black            | Init didn't reach `0xAF`, or charge pump cmd (`0x8D 0x14`) lost       |
| Display fully white "snow"           | Init aborted early — scope SCL, confirm 0xAF is the last command      |
| Image shows only top 1/8 strip       | Multiplex ratio wrong — should be `0xA8 0x3F` for 128x64              |
| Image shifted or wrong page          | Page mode leftover — re-issue `0x20 0x00` (horizontal)                |
| Image mirrored / upside-down         | Swap `0xA1`↔`0xA0` and/or `0xC8`↔`0xC0` in init                       |
| Garbled bytes during long writes     | Clock-stretching disabled on panel side; lower bus to 100 kHz         |
| Bus locks up after a glitched write  | Generate 9 SCL pulses with SDA floated, then `HAL_I2C_Init` again     |

Logic-analyzer checklist:

- One I2C transaction per command/data burst. For a full flush, you
  see a short cmd burst (`[0x78 W][0x00][0x21 0 127][0x22 0 7][stop]`)
  immediately followed by a long data burst
  (`[0x78 W][0x40][1024 bytes][stop]`).
- The data burst should be one continuous transfer; gaps mean HAL is
  splitting it (timeout / interrupt priority issues).

---

## Build wiring into PlatformIO

```ini
[env:nucleo_l476rg_oled_demo]
extends         = env:nucleo_l476rg
build_src_filter = +<display/> +<../examples/oled_example.c>
```

Then `pio run -e nucleo_l476rg_oled_demo -t upload`.

For SH1106 128x64 modules, add:

```
-D OLED_PANEL_128x64_SH1106
```

To use a 128x128 SH1107 module, add `-D OLED_PANEL_128x128_SH1107` to
`build_flags` and resize the animation coordinates (or leave them — the
face will appear in the top half).

---

## Not yet implemented

- DMA-based flush (would need I2C DMA wired in CubeMX; blocking is
  fine for the current 10 Hz animation budget)
- Double-buffering
- Bitmap-asset face library
- Hardware scroll (SSD1306 0x26/0x27 — works in this addressing mode)
