# Cosmo Bot

Fully offline conversational chatbot inspired by Anki Cosmo. No cloud APIs — everything runs on-device.

- **STM32L476 Nucleo-64** — audio I/O, OLED display, on-device keyword spotting (TFLite Micro)
- **ESP32-C3-DevKitM-1** — dialogue engine + audio clip storage in 4 MB flash
- UART link between the two boards (line-delimited text protocol, 115200 baud)

## Status

**Phase 1 — UART bring-up.** Both boards swap test messages over the link.

Phases ahead:

2. I²S microphone capture + Edge Impulse KWS on the L476
3. Intent engine + response templates on the ESP32-C3
4. SSD1306 OLED + TTS audio playback (clips stored on the ESP32-C3, streamed over UART)
5. End-to-end integration

## Layout

```
cosmo-stm32/   PlatformIO + STM32Cube HAL framework, Nucleo-L476RG
cosmo-esp32/   PlatformIO + Arduino-ESP32, ESP32-C3-DevKitM-1
```

Open each subfolder as its own PlatformIO project.

## Wiring (Phase 1)

| Nucleo-L476RG             | ESP32-C3-DevKitM-1 |
| ------------------------- | ------------------ |
| PA9  USART1_TX (CN10-21)  | GPIO5 Serial1 RX   |
| PA10 USART1_RX (CN10-33)  | GPIO4 Serial1 TX   |
| GND                       | GND                |

Both boards run 3.3 V logic — direct connection, no level shifter.

## Build

```bash
cd cosmo-stm32 && pio run -t upload && pio device monitor
cd cosmo-esp32 && pio run -t upload && pio device monitor
```

Press B1 (blue button) on the Nucleo to send a `USER_INPUT`; the ESP32-C3 replies with a hardcoded `BOT_RESPONSE` and LD2 toggles on receipt.

## Protocol

Line-delimited ASCII over UART at 115200 8N1:

```
STM32 -> ESP32 :  [USER_INPUT: <text>]\r\n
ESP32 -> STM32 :  [BOT_RESPONSE: <text>]\r\n
```

A binary audio-streaming sub-channel will be added in Phase 4 for TTS playback.
