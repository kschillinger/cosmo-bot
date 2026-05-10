/**
 * @file    response_database.h
 * @brief   Mapping from dialogue-engine response text to embedded audio clips.
 *
 * The actual storage definition (the const array) lives in
 * src/response_database.c. This header just declares the externs so the
 * lookup code in tts.c can see them.
 *
 * To add a new response:
 *   1. Record/generate a .wav file (16 kHz, mono, 16-bit).
 *   2. Run tools/wav_to_c.py to embed it in response_clips.h.
 *   3. Append a new {.response_text, .audio_data, ...} row in
 *      src/response_database.c.
 *   4. Recompile.
 */

#ifndef COSMO_TTS_RESPONSE_DATABASE_H
#define COSMO_TTS_RESPONSE_DATABASE_H

#include "tts.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const response_clip_t response_database[];
extern const uint32_t        response_database_size;

#ifdef __cplusplus
}
#endif

#endif /* COSMO_TTS_RESPONSE_DATABASE_H */
