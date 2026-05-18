# cosmo-esp32 — ESP32-C3 dialogue MCU

The ESP32-C3 half of Cosmo Bot. Receives recognized user utterances from
the STM32 over UART, classifies them with a tiny keyword-overlap intent
classifier, and replies with a randomly-chosen response variant in
Cosmo's voice.

## What's here

| File | Purpose |
|---|---|
| `src/main.cpp` | Arduino entry. Configures `Serial` (USB-CDC debug) and `Serial1` (link to STM32). Reads `[USER_INPUT: ...]` lines, routes them through `dialogue_respond()`, replies with `[BOT_RESPONSE: ...]`. |
| `src/dialogue.h` / `src/dialogue.cpp` | Classifier and response picker. Pure C++ — the only Arduino-specific bit is `esp_random()`. |
| `src/intents.h` / `src/intents.cpp` | Intent taxonomy. All keyword lists and response variants live here — adding a new intent or response is a single-file change (plus appending to the enum in `intents.h`). |
| `platformio.ini` | Board, framework, USB-CDC build flags, monitor settings. |

## Hardware

- Board: ESP32-C3-DevKitM-1 (USB-C, native USB-CDC, no external UART chip).
- Link to STM32:
  - `GPIO4` (TX1) -> Nucleo `PA10` (USART1_RX)
  - `GPIO5` (RX1) <- Nucleo `PA9`  (USART1_TX)
  - GND <-> Nucleo GND
- Debug to host PC: the same USB-C port (native CDC).

## Build, flash, monitor

```bash
cd cosmo-esp32
pio run                              # compile
pio run -t upload                    # flash via native USB
pio device monitor                   # open USB-CDC monitor
```

### If the monitor is blank after a flash

This was a real problem until the Phase 3 fix. The ESP32-C3 boots
through several USB enumeration steps before the host PC reopens the
COM port, and any prints in that window get dropped. We now compensate
in two places:

1. In code (`main.cpp`):
   - `Serial.setTxTimeoutMs(0)` so `Serial.print()` is non-blocking and
     the sketch doesn't hang forever when the monitor isn't open.
   - `delay(2000)` before the banner so the host has time to attach.
   - `Serial.flush()` after the banner to push it out before anything
     else competes for the bus.

2. In `platformio.ini`:
   - `ARDUINO_USB_MODE=1` + `ARDUINO_USB_CDC_ON_BOOT=1` route `Serial`
     to the chip's hardware USB-CDC.
   - `monitor_dtr = 0` + `monitor_rts = 0` prevent the chip from being
     reset into the bootloader every time the host opens the port.
   - `monitor_filters` is deliberately omitted. The previous
     `esp32_exception_decoder` filter buffers stdout to look for crash
     backtraces, and on a freshly-reset C3 it eats the first ~1 s of
     prints. The `platformio.ini` comments document exactly what to
     re-add (temporarily) when you do need backtrace decoding.

If you still see nothing:
- Unplug and re-plug the USB-C cable, then `pio device monitor`.
- Confirm the chip enumerates as `VID:PID 303A:1001` (Espressif
  USB JTAG/Serial). On Linux: `ls /dev/ttyACM*`. On Windows: open
  Device Manager.

## Dialogue engine — at a glance

```
"What's your name?"
       |
       v
normalize -> "what s your name"
       |
       v
tokenize -> ["what", "s", "your", "name"]
       |
       v
score each intent vs. its keyword list
       |
       v
INTENT_IDENTITY wins (keyword "name" matched, 25% overlap)
       |
       v
random response variant, avoiding immediate repeat
       |
       v
"Name's Cosmo. Friendly robot, mostly."
```

See `../docs/dialogue-engine.md` for the design rationale and step-by-step
instructions for adding new intents.

## Wire-protocol contract

This MCU expects exactly:

| Direction | Format |
|---|---|
| Receive (on `Serial1`) | `[USER_INPUT: <text>]\n` |
| Send (on `Serial1`)    | `[BOT_RESPONSE: <text>]\r\n` |

Anything else on the link is logged to USB-CDC and ignored.

## Quick testing without the STM32

If you want to exercise the dialogue engine standalone (no Nucleo
attached), open a USB-serial adapter on `GPIO4` / `GPIO5` and inject
lines manually:

```
[USER_INPUT: hello]
[USER_INPUT: who are you]
[USER_INPUT: tell me a joke]
[USER_INPUT: thanks]
[USER_INPUT: blarg flornk]           # should hit INTENT_FALLBACK
```

The USB-CDC monitor will print the parsed text, the classified intent
and confidence, and the chosen response.
