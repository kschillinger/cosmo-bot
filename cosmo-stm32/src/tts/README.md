# TTS Subsystem (Cosmo / STM32L476)

Pre-recorded clip lookup TTS for the Cosmo conversational chatbot. Given a
response string from the dialogue engine, this layer finds the matching
pre-recorded PCM clip in Flash and exposes a pointer the audio playback
subsystem can hand straight to the DAC.

---

## Layout

```
cosmo-stm32/
├── src/tts/
│   ├── tts.{h,c}                Public API + layered implementation
│   ├── response_database.{h,c}  Text → clip mapping table
│   ├── response_clips.{h,c}     Embedded PCM (placeholder; regenerated)
│   └── README.md                This file
└── tools/
    └── wav_to_c.py              .wav → embedded C-array converter
```

PlatformIO recurses `src/` automatically, so dropping the module under
`src/tts/` is enough — no project-file edits needed.

## Audio format

Every clip in the database must be:

- **16-bit signed PCM**
- **16 kHz** sample rate
- **mono**

`wav_to_c.py` rejects anything else loudly. Do resampling/normalisation on
the host (Audacity, `ffmpeg -ar 16000 -ac 1 -sample_fmt s16`) before
running it.

## Recording → embedding workflow

1. Record or generate each response as a `.wav` (Google TTS, espeak, voice
   actor — any source works).
2. In Audacity: normalise to **−3 dB**, resample to **16 kHz mono 16-bit**,
   export as `.wav`.
3. Drop them into a `responses/` folder following the naming used in
   `response_database.c` (`greeting_001.wav`, `joke_001.wav`, etc.).
4. Run the converter:
   ```
   python3 cosmo-stm32/tools/wav_to_c.py responses/*.wav \
           -o cosmo-stm32/src/tts/response_clips.h
   ```
5. Delete `cosmo-stm32/src/tts/response_clips.c` once the generator emits
   real arrays — the placeholder silence buffer is no longer needed.
6. Rebuild firmware. Watch the linker output for total Flash use; budget
   ~32 KB per second of audio.

## Adding a new response

1. Record + convert as above.
2. Append a new row in `src/tts/response_database.c`:
   ```c
   {
       .response_text   = "Exact string from dialogue engine",
       .audio_data      = my_clip_audio,
       .audio_size      = my_clip_size,
       .duration_ms     = my_clip_duration_ms,
       .source_filename = "responses/my_clip.wav"
   },
   ```
3. Recompile.

The `.response_text` field must match the dialogue engine output
**byte-for-byte** for the fast path. The fuzzy fallback in
`tts_find_response()` handles minor drift (substring containment).

## Public API

```c
void           tts_init(void);
void           tts_process(const char *response_text);
uint8_t        tts_is_complete(void);
const int16_t *tts_get_audio_buffer(void);
uint32_t       tts_get_audio_size(void);
uint16_t       tts_get_audio_duration_ms(void);
uint8_t        tts_get_error_code(void);
const char    *tts_get_error_message(void);
void           tts_cancel(void);
```

`tts_process()` is synchronous and returns in well under 100 ms — there's
no I/O, just a string compare and a Flash pointer assignment.

## FSM integration

```c
/* PROCESSING_TTS */
void fsm_execute_processing_tts(void) {
    if (tts_is_complete()) {
        audio_play_buffer(tts_get_audio_buffer(),
                          tts_get_audio_size());
        fsm.next_state = FSM_STATE_PLAYBACK;
        return;
    }
    if (tts_get_error_code() != TTS_ERROR_NONE) {
        fsm.next_state = FSM_STATE_ERROR;
        return;
    }
    if (system_is_timeout(fsm.state_entry_time, 3000)) {
        tts_set_error(TTS_ERROR_TIMEOUT, "TTS exceeded 3 s");
        fsm.next_state = FSM_STATE_ERROR;
    }
}

/* PLAYBACK */
void fsm_execute_playback(void) {
    if (audio_playback_is_done()) {
        fsm.next_state = FSM_STATE_IDLE;
    }
}
```

## BSP hook

`tts.c` provides weak fallbacks for `system_get_tick_ms()` and
`tts_log()` so it builds standalone for host tests. Once the real BSP
supplies these, define `TTS_HAS_BSP` in `platformio.ini` build flags so
the weak fallbacks don't shadow them:

```
build_flags =
    ...
    -D TTS_HAS_BSP
```

## Memory budget (10 clips × ~3 s each)

| Region           | Use      |
|------------------|----------|
| RAM (context)    | ~3 KB    |
| Flash (audio)    | ~75–100 KB at 16-bit / 16 kHz |
| Flash (code)     | ~4 KB    |

Keep the database to 5–8 short clips for the first integration to stay
well clear of the 256 KB Flash ceiling. SD-card overflow is sketched in
`tts_load_audio_from_sd()` for the next iteration.

## Errors

| Code | Meaning                       |
|------|-------------------------------|
| 0    | `TTS_ERROR_NONE`              |
| 1    | `TTS_ERROR_RESPONSE_NOT_FOUND`|
| 2    | `TTS_ERROR_AUDIO_LOAD_FAILED` |
| 3    | `TTS_ERROR_INVALID_INPUT`     |
| 4    | `TTS_ERROR_BUFFER_OVERFLOW`   |
| 5    | `TTS_ERROR_TIMEOUT`           |

Inspect with `tts_get_error_code()` / `tts_get_error_message()`. Every
state transition and error is logged through the weak `tts_log()` hook.
