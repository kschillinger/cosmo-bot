/**
 * tts.c — STUB. See tts.h.
 *
 * Synthesis "completes" ~400 ms after tts_process(). Returns a non-empty
 * dummy audio buffer so FSM empty-checks pass and PLAYBACK can proceed.
 */

#include "tts.h"
#include "system_utils.h"

#include <stdio.h>
#include <string.h>

#define STUB_TTS_DURATION_MS  400U
#define STUB_TTS_AUDIO_BYTES  32

static uint8_t  s_audio[STUB_TTS_AUDIO_BYTES];
static uint8_t  s_synthesis_active   = 0;
static uint32_t s_synthesis_start_ms = 0;

void tts_init(void)
{
    printf("[TTS] tts_init() (stub)\r\n");
    memset(s_audio, 0, sizeof(s_audio));
}

void tts_process(const char *text)
{
    printf("[TTS] tts_process(\"%s\") (stub)\r\n", text ? text : "(null)");
    s_synthesis_active   = 1;
    s_synthesis_start_ms = system_get_tick_ms();
}

uint8_t tts_is_complete(void)
{
    if (!s_synthesis_active) return 0;
    if ((system_get_tick_ms() - s_synthesis_start_ms) >= STUB_TTS_DURATION_MS) {
        s_synthesis_active = 0;
        return 1;
    }
    return 0;
}

uint8_t *tts_get_audio_buffer(void)
{
    return s_audio;
}

uint16_t tts_get_audio_size(void)
{
    return STUB_TTS_AUDIO_BYTES;
}
