# Cosmo Bot — Top-Level FSM

This is the orchestration FSM that runs on the STM32L476 and drives one full
conversational round-trip with the user. The FSM is intentionally
hardware-agnostic: every input or output goes through a subsystem header
(`audio.h`, `stt.h`, `uart_comm.h`, `tts.h`, `oled_display.h`,
`system_utils.h`), each of which can be stubbed or replaced independently.

## States

| State              | What it does                                 | Display                   |
|--------------------|----------------------------------------------|---------------------------|
| `IDLE`             | Wait for B1 press (later: sound threshold)   | Cute "ready" face         |
| `LISTENING`        | Microphone capture (≤ 6 s)                   | Live waveform             |
| `PROCESSING_STT`   | Local speech-to-text inference               | Spinner                   |
| `SENDING_TO_ESP32` | UART round-trip to dialogue engine           | Spinner + recognized text |
| `PROCESSING_TTS`   | Render response audio                        | Spinner                   |
| `PLAYBACK`         | Play synthesized audio out the speaker       | Mouth animation + text    |
| `ERROR`            | Display error for ~3 s, then return to IDLE  | Sad face + message        |

## Transition diagram

```
         ┌──────────────────────────┐
         │           IDLE           │◄────────────────────────────┐
         └────────────┬─────────────┘                             │
                      │ B1 press / sound detected                 │
                      ▼                                           │
         ┌──────────────────────────┐                             │
         │        LISTENING         │ ── timeout(6s) ──┐          │
         └────────────┬─────────────┘                  │          │
                      │ capture done                   │          │
                      ▼                                ▼          │
         ┌──────────────────────────┐         ┌───────────────┐   │
         │      PROCESSING_STT      │ ── x ──►│     ERROR     │   │
         └────────────┬─────────────┘         │ (3s timeout)  ├───┘
                      │ STT result ok                  ▲    
                      ▼                                │    
         ┌──────────────────────────┐                  │    
         │     SENDING_TO_ESP32     │ ── x ────────────┤    
         └────────────┬─────────────┘                  │    
                      │ BOT_RESPONSE received          │    
                      ▼                                │    
         ┌──────────────────────────┐                  │    
         │      PROCESSING_TTS      │ ── x ────────────┤    
         └────────────┬─────────────┘                  │    
                      │ TTS audio ready                │    
                      ▼                                │    
         ┌──────────────────────────┐                  │    
         │         PLAYBACK         │ ── timeout ──────┘    
         └────────────┬─────────────┘                       
                      │ playback done                       
                      └───► IDLE                            
```

`x` = subsystem failure or per-state timeout. PLAYBACK timeout is benign — it
returns to IDLE rather than escalating to ERROR.

## Per-state timeouts

Defined in `fsm.h`:

| State              | Timeout | Macro                              |
|--------------------|---------|------------------------------------|
| `IDLE`             | none    | (set to 0)                         |
| `LISTENING`        | 6 s     | `FSM_TIMEOUT_LISTENING_MS`         |
| `PROCESSING_STT`   | 3 s     | `FSM_TIMEOUT_PROCESSING_STT_MS`    |
| `SENDING_TO_ESP32` | 5 s     | `FSM_TIMEOUT_SENDING_ESP32_MS`     |
| `PROCESSING_TTS`   | 3 s     | `FSM_TIMEOUT_PROCESSING_TTS_MS`    |
| `PLAYBACK`         | 10 s    | `FSM_TIMEOUT_PLAYBACK_MS`          |
| `ERROR`            | 3 s     | `FSM_TIMEOUT_ERROR_MS`             |

## Error codes

```c
FSM_ERROR_NONE                  0
FSM_ERROR_AUDIO_CAPTURE_FAILED  1
FSM_ERROR_STT_FAILED            2
FSM_ERROR_UART_TIMEOUT          3
FSM_ERROR_UART_ERROR            4
FSM_ERROR_TTS_FAILED            5
FSM_ERROR_PLAYBACK_FAILED       6
FSM_ERROR_UNKNOWN               99
```

## Build

This is a PlatformIO project; everything in `cosmo-stm32/src/` is auto-built.
No separate Makefile is required.

```
cd cosmo-stm32
pio run -e nucleo_l476rg -t upload
pio device monitor -e nucleo_l476rg
```

## Subsystem stub status

| Subsystem      | Status      | Notes                                          |
|----------------|-------------|------------------------------------------------|
| `audio.c`      | stub        | Fakes capture/playback completion after 500 ms |
| `stt.c`        | stub        | Returns "hello cosmo" after 300 ms             |
| `uart_comm.c`  | **real**    | Wraps existing `uart_link.c` (Phase 1)         |
| `tts.c`        | stub        | Returns dummy 32-byte buffer after 400 ms      |
| `oled_display.c` | stub      | Logs to USART2 only (rate-limited)             |
| `system_utils.c` | **real**  | HAL wrappers + B1 debounce                     |

So, out of the box, pressing B1 will run the FSM through every state with
fake but non-empty data, and a real BOT_RESPONSE round-trip via the ESP32-C3
UART link.
