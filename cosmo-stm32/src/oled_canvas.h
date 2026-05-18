#ifndef OLED_CANVAS_H
#define OLED_CANVAS_H

#include <stdint.h>

#define OLED_CANVAS_WIDTH   128U
#define OLED_CANVAS_HEIGHT  64U
#define OLED_CANVAS_PIXELS  (OLED_CANVAS_WIDTH * OLED_CANVAS_HEIGHT)
#define OLED_CANVAS_BYTES   (OLED_CANVAS_PIXELS / 8U)

void oled_canvas_clear(uint8_t *framebuffer, uint8_t on);
void oled_canvas_set_pixel(uint8_t *framebuffer, uint8_t x, uint8_t y, uint8_t on);
uint8_t oled_canvas_get_pixel(const uint8_t *framebuffer, uint8_t x, uint8_t y);

void oled_canvas_render_idle(uint8_t *framebuffer);
void oled_canvas_render_listening(uint8_t *framebuffer,
                                  const uint16_t *samples,
                                  uint16_t sample_count);
void oled_canvas_render_processing(uint8_t *framebuffer, uint16_t frame);
void oled_canvas_render_responding(uint8_t *framebuffer, uint16_t frame);
void oled_canvas_render_error(uint8_t *framebuffer);

#endif /* OLED_CANVAS_H */
