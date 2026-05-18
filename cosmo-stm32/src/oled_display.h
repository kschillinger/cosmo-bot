/**
 * oled_display.h — OLED display subsystem (SSD1306, 128x64, I2C).
 */

#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdint.h>

void oled_init(void);

void oled_display_idle(void);
void oled_display_listening(uint16_t *samples, uint16_t sample_count);
void oled_display_processing(uint16_t frame);
void oled_display_responding(const char *text, uint16_t frame);
void oled_display_error(const char *error_message);

void oled_update_display(void);

#endif /* OLED_DISPLAY_H */
