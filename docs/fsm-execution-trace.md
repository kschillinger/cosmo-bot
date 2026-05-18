# FSM Execution Trace — Expected Output

This is what `pio device monitor` should show after flashing the skeleton and
pressing B1. Times are approximate (driven by the stub timings in
`audio.c` / `stt.c` / `tts.c`); only the UART round-trip depends on the
ESP32-C3 actually replying.

## Boot

```
==========================================
 Cosmo Bot — STM32L476RG  Phase 2 (FSM)
 SYSCLK = 80000000 Hz
 Press B1 to start a conversational round.
==========================================
[FSM] fsm_init()
[AUDIO] audio_init() (stub)
[STT]   stt_init() (stub)
[UART]  uart_init() -> UartLink_Init()
[TTS]   tts_init() (stub)
[OLED] SSD1306 init complete (I2C1 PB8/PB9, addr=0x3C)
[FSM] init complete; entering IDLE
[1ms] [FSM] ? -> IDLE
[FSM] enter IDLE
```

## After pressing B1

```
[3471ms] [SYS]  button edge (B1 pressed)
[3471ms] [FSM] IDLE: button edge -> LISTENING
[3481ms] [FSM] IDLE -> LISTENING
[FSM] enter LISTENING
[AUDIO] audio_capture_start() (stub)

  ... ~500 ms of waveform updates ...

[3982ms] [AUDIO] audio_capture_stop() (stub)
[3982ms] [FSM] LISTENING -> PROCESSING_STT
[FSM] enter PROCESSING_STT
[AUDIO] audio_get_buffer() (stub)
[STT]   stt_process(size=16) (stub)

  ... ~300 ms of spinner ...

[4283ms] [STT]   stt_get_result() -> "hello cosmo" (stub)
[4283ms] [FSM]   STT -> "hello cosmo"
[4283ms] [FSM]   PROCESSING_STT -> SENDING_TO_ESP32
[FSM] enter SENDING_TO_ESP32  txt="hello cosmo"
[UART]  uart_send_message("hello cosmo")

  ... waits for real ESP32 reply over USART1 ...

[4612ms] [UART]  message_available -> "Hi there, friend!"
[4612ms] [FSM]   ESP32 -> "Hi there, friend!"
[4612ms] [FSM]   SENDING_TO_ESP32 -> PROCESSING_TTS
[FSM] enter PROCESSING_TTS  txt="Hi there, friend!"
[TTS]   tts_process("Hi there, friend!") (stub)

  ... ~400 ms ...

[5013ms] [FSM] PROCESSING_TTS -> PLAYBACK
[FSM] enter PLAYBACK
[AUDIO] audio_play_buffer(size=32) (stub)

  ... ~500 ms ...

[5514ms] [FSM] PLAYBACK -> IDLE
[FSM] enter IDLE
```

## Total round-trip (with stubs)

~2 s end-to-end, dominated by the UART wait. With real audio capture
(2–5 s) and real ML inference (still well under 1 s on TFLite Micro),
expect 4–7 s per round.

## Timeout behavior

If you disconnect the ESP32 and press B1, the FSM should land in ERROR
after ~5 s:

```
[FSM] enter SENDING_TO_ESP32  txt="hello cosmo"
[UART]  uart_send_message("hello cosmo")
  ... 5 s ...
[FSM] !! error 3: ESP32 UART timeout
[FSM] SENDING_TO_ESP32 -> ERROR
[FSM] enter ERROR  code=3 msg="ESP32 UART timeout"
  ... 3 s ...
[FSM] ERROR: display interval elapsed, returning to IDLE
[FSM] ERROR -> IDLE
```

This is exactly what Phase 3 of the testing strategy in the spec asks for.
