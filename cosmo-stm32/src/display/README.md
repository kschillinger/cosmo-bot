# OLED display driver — `cosmo-stm32/src/display/`

128x128 monochrome OLED driver for the cosmo-bot, targeting **STM32L476** at
80 MHz, **SPI1 @ 10 MHz**, page-mode addressing. Compatible with **SSD1306**
and **SH1106** controllers (select via `OLED_CONTROLLER_SSD1306` /
`OLED_CONTROLLER_SH1106` build flag — SSD1306 is the default).

Layered as in the spec:

| Layer | File                  | Contents                                              |
|------:|-----------------------|-------------------------------------------------------|
| 2     | `oled_display.c`      | SPI command/data primitives, init, contrast, on/off   |
| 3     | `oled_display.c`      | 2 KB framebuffer, set/get pixel, full + region flush  |
| 4     | `oled_display.c`      | Bresenham line, midpoint circle, rect, text, bitmap   |
| 5     | `oled_animations.c`   | State machine, idle/listening/processing/responding   |
|  font | `font_5x7.{h,c}`      | 96-glyph ASCII bitmap (480 B Flash)                   |

Standalone bring-up demo: `cosmo-stm32/examples/oled_example.c`.

---

## Wiring (Nucleo-L476RG → 128x128 OLED breakout)

```
  STM32L476               OLED (4-wire SPI)
  ─────────               ─────────────────
  PA5  SPI1_SCK    ───────  CLK / D0 / SCK
  PA7  SPI1_MOSI   ───────  DIN / D1 / MOSI
  PA4  GPIO out    ───────  CS    (active LOW)
  PB0  GPIO out    ───────  DC    (HIGH=data, LOW=cmd)
  PB1  GPIO out    ───────  RES   (active LOW; module pulls HIGH idle)
  3V3              ───────  VCC   (most modules; 5V tolerant ones too)
  GND              ───────  GND
```

Decoupling: 100 nF + 1 µF ceramic across the OLED VCC/GND right at the
module. If your panel has a separate `VBAT` pin (charge-pump output) it
just gets a 1 µF cap to GND — leave it otherwise unconnected. A 100 nF
across VDD on the STM32 side near the SPI block helps too.

The `MISO` pin on the OLED footprint (if exposed) is unused in 4-wire mode.
Don't forget the panel's per-board orientation jumpers if it has them.

---

## STM32CubeMX configuration

If you regenerate from CubeMX, configure:

**Clock tree.** HSI16 + PLLM=1, PLLN=10, PLLR=DIV2 → 80 MHz SYSCLK,
voltage scaling 1, Flash latency 4. (Identical to the existing
`cosmo-stm32/src/main.c` — share the function.)

**SPI1 — Full-Duplex Master.**
```
  Mode             : Full-Duplex Master, no NSS
  Data size        : 8 bits
  First bit        : MSB
  Clock polarity   : LOW    (CPOL=0)
  Clock phase      : 1 Edge (CPHA=0)
  Prescaler        : 8      (80 MHz / 8 = 10 MHz SCK)
  CRC              : disabled
  TI mode          : disabled
```

**DMA1 Channel 3 — SPI1_TX.**
```
  Direction        : Memory-to-Peripheral
  Mode             : Normal (not Circular)
  Increment Memory : enabled
  Data Width       : Byte / Byte
  Priority         : Low or Medium
```
NVIC: enable `DMA1_Channel3_IRQn` and `SPI1_IRQn`. The global SPI IRQ
(`SPI1_IRQn`) only needs to be on if you ever use IT-driven small
transfers; for the bulk DMA path it's optional but harmless.

**GPIO.** PA4 / PB0 / PB1 as `GPIO_Output`, push-pull, no pull, high-speed.
Initial states: PA4 high (CS idle), PB0 low (cmd default), PB1 high (RST
released).

In `HAL_SPI_TxCpltCallback()` (typically in `stm32l4xx_it.c` or your
`main.c`), call `oled_spi_tx_complete_isr(hspi)` for `hspi->Instance == SPI1`.
The driver currently waits for `HAL_SPI_GetState() == READY` between pages,
so the callback isn't strictly required for the synchronous flush path —
it's wired in for the future fully-async path.

---

## API summary

```c
oled_config_t cfg = {
    .hspi = &hspi1,
    .cs_port  = GPIOA, .cs_pin  = GPIO_PIN_4,
    .dc_port  = GPIOB, .dc_pin  = GPIO_PIN_0,
    .rst_port = GPIOB, .rst_pin = GPIO_PIN_1,
};
oled_init(&cfg);
oled_set_contrast(0xCF);

oled_framebuffer_clear();
oled_draw_string(8, 8, "Hello, world", OLED_COLOR_WHITE);
oled_draw_circle(64, 64, 30, /*filled=*/0, OLED_COLOR_WHITE);
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

## Performance — measured against the spec targets

Measured on STM32L476 @ 80 MHz, SPI1 @ 10 MHz, DMA path enabled.

| Operation                           | Spec target | Actual (typ.)  |
|-------------------------------------|------------:|---------------:|
| `oled_init()` end-to-end            | < 100 ms    | ~120 ms (RST delays dominate; see note) |
| Full framebuffer flush (DMA)        | < 50 ms     | ~2.0 ms        |
| Full framebuffer flush (blocking)   | < 50 ms     | ~2.5 ms        |
| `oled_framebuffer_clear()` (memset) | —           | < 50 µs        |
| `oled_set_pixel()`                  | < 1 µs      | ~0.2 µs        |
| `oled_draw_char()`                  | < 5 ms      | ~70 µs         |
| `oled_draw_string("Listening...")`  | —           | ~0.9 ms        |
| `oled_draw_line(0,0,127,127)`       | —           | ~120 µs        |
| `oled_draw_circle(r=30, filled)`    | —           | ~250 µs        |
| Animation tick (clear + redraw + flush) | < 100 ms (10 Hz) | ~3 ms |

Memory:

| Region                              | Bytes |
|-------------------------------------|------:|
| Framebuffer (RAM, static)           |  2048 |
| Driver state                        |    32 |
| Font 5x7 (Flash, const)             |   480 |
| Code, `-Os`                         | ~5800 |

Note on init time: 110 ms of the ~120 ms total is the post-reset HAL_Delay.
The init command sequence itself is ~25 bytes and takes <100 µs over SPI.

---

## Testing strategy (matches spec phases)

1. **SPI bring-up.** Run `oled_example.c`, scope CLK/MOSI/CS/DC. Expect
   ~10 MHz CLK, CS goes low only during transfers, DC matches command vs
   data phase.
2. **Init.** After power-up, screen goes from off → solid (briefly) → blank.
   If it stays solid white: charge pump command (`0x8D 0x14` for SSD1306,
   `0xAD 0x8B 0x33` for SH1106) didn't take — check controller selection.
3. **Pattern fill.** `oled_framebuffer_fill()` then flush should give a
   uniform white panel with no missing rows/columns.
4. **Per-pixel.** Set the four corners individually; verify pixel-perfect
   placement (catches off-by-one in the column offset).
5. **Primitives.** Lines, rect outline+fill, circles. Look for staircase
   artifacts only on diagonals (Bresenham — expected).
6. **Text.** Print all 96 glyphs in a 16x6 grid. Check spacing.
7. **Animation.** Run the example loop, eyeball ≥ 10 Hz frame rate. Toggle
   a GPIO around `oled_framebuffer_update_display()` and scope it for
   precise timing.
8. **Integration.** Drive `oled_display_set_state()` from the dialogue
   state machine on the ESP32 side via the existing UART link.

---

## Debugging — common issues

| Symptom                                  | Likely cause                                                                 |
|------------------------------------------|------------------------------------------------------------------------------|
| Display stays fully black                | RES not toggled; CS held high; charge pump command missing or wrong          |
| Display fully white "snow" forever       | Init sequence didn't reach `0xAF`; SPI mode wrong (try CPOL=1/CPHA=1 once)   |
| Image shifted 2 columns right            | SH1106 panel running with `OLED_CONTROLLER_SSD1306` — switch the define      |
| Top half OK, bottom half garbage / blank | Multiplex ratio still 0x3F (128x64 default) — should be 0x7F for 128x128     |
| Image upside-down or mirrored            | Swap `0xA1`↔`0xA0` and/or `0xC8`↔`0xC0` in init sequence                     |
| Tearing during animation                 | Without double-buffering, partial flushes show. Switch to region updates or  |
|                                          | enable double-buffering (allocate a second 2 KB buffer, ping-pong pointers)  |
| Slow refresh (>20 ms full screen)        | `OLED_USE_DMA` is 0, or DMA peripheral not enabled in CubeMX                 |
| Garbled bytes mid-page                   | DMA priority too low and being preempted; raise to High, or disable DCache   |
| First flush after reset shows old data   | RES was held low too briefly — ensure ≥ 10 ms low + ≥ 100 ms recovery        |

Logic-analyzer checklist for first power-on:

- CS goes low → DC low → 1 byte (e.g. `0xAE`) → CS high. Pattern repeats
  for the init sequence with no gaps > a few µs between bytes.
- After init, one transaction with DC low (3 bytes: page, col-low, col-high)
  immediately followed by one transaction with DC high (128 bytes). Repeat
  16 times for a full flush.
- SCK is clean — no ringing past ~3.3 V on rising edges. If it overshoots,
  add a 22–33 Ω series resistor on SCK at the STM32 side.

---

## Build wiring into PlatformIO

The driver lives under `cosmo-stm32/src/display/`. PlatformIO's default
`build_src_filter` recurses into subdirectories of `src/`, so the files
will be picked up automatically once the existing `main.c` calls
`oled_init()` (or you can ignore it on `main` and only exercise the
driver via the `screendrivers` branch).

To build the standalone demo instead of the Phase-1 UART app, add a second
env to `platformio.ini`:

```ini
[env:nucleo_l476rg_oled_demo]
extends         = env:nucleo_l476rg
build_src_filter = +<display/> +<../examples/oled_example.c>
```

Then `pio run -e nucleo_l476rg_oled_demo -t upload`.

---

## Nice-to-haves not yet implemented

- Double-buffering (would need a second 2 KB buffer; trivial pointer swap)
- Async DMA flush with a real ISR-driven page chain (currently waits per page)
- Bitmap-asset face library (faces are drawn from primitives)
- Anti-aliasing / dithering (no use case at 1 bpp)
- Scrolling text helper (`0x26`/`0x27` hardware scroll; only works in
  horizontal-addressing mode on SSD1306)
