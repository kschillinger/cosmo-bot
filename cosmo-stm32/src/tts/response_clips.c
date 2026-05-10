/**
 * @file    response_clips.c
 * @brief   Placeholder PCM data backing the macros in response_clips.h.
 *
 * Replaced wholesale by the output of tools/wav_to_c.py once the real .wav
 * recordings exist. Until then, a single shared 200 ms silence buffer is
 * aliased by every clip macro, which keeps the link clean without bloating
 * Flash.
 */

#include "response_clips.h"

const int16_t tts_placeholder_silence[TTS_PLACEHOLDER_SAMPLES] = { 0 };
