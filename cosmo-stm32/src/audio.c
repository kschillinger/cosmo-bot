/**
 * audio.c — STUB. See audio.h.
 *
 * Stubs are wired so the FSM cycles cleanly during Phase 1 testing:
 *   - capture "completes" ~500 ms after start
 *   - playback "completes" ~500 ms after start
 *   - get_buffer / get_samples return small static dummy buffers (non-NULL
 *     so FSM empty-checks don't trip)
 *
 * Replace this file when the real I2S/SAI capture path is ready.
 */

#include "audio.h"
#include "system_utils.h"

#include <stdio.h>
#include <string.h>

#define DUMMY_AUDIO_BYTES   16
#define DUMMY_SAMPLE_COUNT  16
#define STUB_CAPTURE_MS     500U
#define STUB_PLAYBACK_MS    500U

static uint8_t  s_audio_buf[DUMMY_AUDIO_BYTES];
static uint16_t s_samples[DUMMY_SAMPLE_COUNT];

static uint8_t  s_capture_active   = 0;
static uint32_t s_capture_start_ms = 0;
static uint8_t  s_playback_active  = 0;
static uint32_t s_playback_start_ms = 0;

void audio_init(void)
{
    printf("[AUDIO] audio_init() (stub)\r\n");
    memset(s_audio_buf, 0, sizeof(s_audio_buf));
    memset(s_samples,   0, sizeof(s_samples));
    s_capture_active  = 0;
    s_playback_active = 0;
}

/* --- Capture -------------------------------------------------------------- */

void audio_capture_start(void)
{
    printf("[AUDIO] audio_capture_start() (stub)\r\n");
    s_capture_active   = 1;
    s_capture_start_ms = system_get_tick_ms();
}

void audio_capture_stop(void)
{
    printf("[AUDIO] audio_capture_stop() (stub)\r\n");
    s_capture_active = 0;
}

uint8_t audio_capture_is_active(void)
{
    if (!s_capture_active) return 0;
    if ((system_get_tick_ms() - s_capture_start_ms) >= STUB_CAPTURE_MS) {
        s_capture_active = 0;  /* fake "capture finished" */
        return 0;
    }
    return 1;
}

uint8_t *audio_get_buffer(void)
{
    printf("[AUDIO] audio_get_buffer() (stub)\r\n");
    return s_audio_buf;
}

uint16_t audio_get_buffer_size(void)
{
    return (uint16_t)sizeof(s_audio_buf);
}

uint16_t *audio_get_samples(void)
{
    return s_samples;
}

uint16_t audio_get_sample_count(void)
{
    return DUMMY_SAMPLE_COUNT;
}

/* --- Playback ------------------------------------------------------------- */

void audio_play_buffer(uint8_t *buffer, uint16_t size)
{
    (void)buffer;
    printf("[AUDIO] audio_play_buffer(size=%u) (stub)\r\n", (unsigned)size);
    s_playback_active   = 1;
    s_playback_start_ms = system_get_tick_ms();
}

uint8_t audio_playback_is_done(void)
{
    if (!s_playback_active) return 1;
    if ((system_get_tick_ms() - s_playback_start_ms) >= STUB_PLAYBACK_MS) {
        s_playback_active = 0;
        return 1;
    }
    return 0;
}
