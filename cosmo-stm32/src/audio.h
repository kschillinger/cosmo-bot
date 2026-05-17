/**
 * audio.h — Microphone capture + speaker playback (STM32L476)
 *
 * Stubbed for FSM bring-up. Real implementation will use I2S/SAI for the
 * MEMS mic and a DAC + class-D amp for the speaker.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

/* --- Lifecycle ------------------------------------------------------------ */
void     audio_init(void);

/* --- Capture (mic) -------------------------------------------------------- */
void     audio_capture_start(void);
void     audio_capture_stop(void);
uint8_t  audio_capture_is_active(void);   /* 1 = still recording, 0 = done   */
uint8_t *audio_get_buffer(void);
uint16_t audio_get_buffer_size(void);
uint16_t *audio_get_samples(void);        /* downsampled for waveform display */
uint16_t audio_get_sample_count(void);

/* --- Playback (speaker) --------------------------------------------------- */
void     audio_play_buffer(uint8_t *buffer, uint16_t size);
uint8_t  audio_playback_is_done(void);    /* 1 = done, 0 = still playing     */

#endif /* AUDIO_H */
