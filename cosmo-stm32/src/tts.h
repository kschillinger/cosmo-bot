/**
 * tts.h — Text-to-Speech (STM32L476)
 *
 * Stubbed for FSM bring-up. Real implementation will either play pre-recorded
 * .wav clips out of flash or stream synthesized PCM (e.g. Talkie / espeak-ng).
 */

#ifndef TTS_H
#define TTS_H

#include <stdint.h>

void     tts_init(void);
void     tts_process(const char *text);
uint8_t  tts_is_complete(void);
uint8_t *tts_get_audio_buffer(void);
uint16_t tts_get_audio_size(void);

#endif /* TTS_H */
