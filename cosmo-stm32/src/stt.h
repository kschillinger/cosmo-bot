/**
 * stt.h — Speech-to-Text (STM32L476)
 *
 * Stubbed for FSM bring-up. Real implementation will use TFLite Micro with a
 * keyword-spotting / small ASR model.
 */

#ifndef STT_H
#define STT_H

#include <stdint.h>

void    stt_init(void);
void    stt_process(uint8_t *audio_buffer, uint16_t buffer_size);
uint8_t stt_is_complete(void);   /* 1 if inference done, 0 if still running */
char   *stt_get_result(void);    /* NUL-terminated; "" on failure           */

#endif /* STT_H */
