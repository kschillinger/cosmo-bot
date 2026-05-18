#include "oled_canvas.h"

#include <stddef.h>
#include <string.h>

static void oled_canvas_draw_line(uint8_t *framebuffer,
                                  uint8_t x0,
                                  uint8_t y0,
                                  uint8_t x1,
                                  uint8_t y1,
                                  uint8_t on);
static void oled_canvas_draw_rect(uint8_t *framebuffer,
                                  uint8_t x,
                                  uint8_t y,
                                  uint8_t w,
                                  uint8_t h,
                                  uint8_t on);
static void oled_canvas_fill_rect(uint8_t *framebuffer,
                                  uint8_t x,
                                  uint8_t y,
                                  uint8_t w,
                                  uint8_t h,
                                  uint8_t on);
static void oled_canvas_draw_circle(uint8_t *framebuffer,
                                    uint8_t cx,
                                    uint8_t cy,
                                    uint8_t radius,
                                    uint8_t on);

void oled_canvas_clear(uint8_t *framebuffer, uint8_t on)
{
    if (framebuffer == NULL) return;
    memset(framebuffer, on ? 0xFF : 0x00, OLED_CANVAS_BYTES);
}

void oled_canvas_set_pixel(uint8_t *framebuffer, uint8_t x, uint8_t y, uint8_t on)
{
    if (framebuffer == NULL) return;
    if (x >= OLED_CANVAS_WIDTH || y >= OLED_CANVAS_HEIGHT) return;

    uint16_t idx = (uint16_t)y * OLED_CANVAS_WIDTH + x;
    uint16_t byte_idx = idx >> 3;
    uint8_t bit = (uint8_t)(7U - (idx & 0x07U));
    uint8_t mask = (uint8_t)(1U << bit);

    if (on) {
        framebuffer[byte_idx] |= mask;
    } else {
        framebuffer[byte_idx] &= (uint8_t)~mask;
    }
}

uint8_t oled_canvas_get_pixel(const uint8_t *framebuffer, uint8_t x, uint8_t y)
{
    if (framebuffer == NULL) return 0;
    if (x >= OLED_CANVAS_WIDTH || y >= OLED_CANVAS_HEIGHT) return 0;

    uint16_t idx = (uint16_t)y * OLED_CANVAS_WIDTH + x;
    uint16_t byte_idx = idx >> 3;
    uint8_t bit = (uint8_t)(7U - (idx & 0x07U));
    return (framebuffer[byte_idx] & (uint8_t)(1U << bit)) ? 1U : 0U;
}

void oled_canvas_render_idle(uint8_t *framebuffer)
{
    oled_canvas_clear(framebuffer, 0);

    oled_canvas_draw_circle(framebuffer, 64U, 32U, 22U, 1U);
    oled_canvas_fill_rect(framebuffer, 54U, 24U, 6U, 6U, 1U); /* left eye */
    oled_canvas_fill_rect(framebuffer, 68U, 24U, 6U, 6U, 1U); /* right eye */
    oled_canvas_draw_line(framebuffer, 54U, 44U, 74U, 44U, 1U); /* smile */
}

void oled_canvas_render_listening(uint8_t *framebuffer,
                                  const uint16_t *samples,
                                  uint16_t sample_count)
{
    uint8_t x;
    oled_canvas_clear(framebuffer, 0);
    oled_canvas_draw_rect(framebuffer, 0U, 0U, OLED_CANVAS_WIDTH, OLED_CANVAS_HEIGHT, 1U);
    oled_canvas_draw_line(framebuffer, 0U, 32U, 127U, 32U, 1U);

    if (samples == NULL || sample_count == 0U) return;

    for (x = 1U; x < 127U; x += 2U) {
        uint16_t src_idx = (uint16_t)(((uint32_t)(x - 1U) * sample_count) / 126U);
        uint16_t amplitude = samples[src_idx];
        uint8_t height = (uint8_t)((amplitude >> 8U) & 0x0FU); /* 0..15 */
        uint8_t y_top = (uint8_t)(32U - height);
        uint8_t y_bottom = (uint8_t)(32U + height);
        oled_canvas_draw_line(framebuffer, x, y_top, x, y_bottom, 1U);
    }
}

void oled_canvas_render_processing(uint8_t *framebuffer, uint16_t frame)
{
    static const uint8_t spinner_x[8] = {64U, 74U, 80U, 74U, 64U, 54U, 48U, 54U};
    static const uint8_t spinner_y[8] = {20U, 24U, 32U, 40U, 44U, 40U, 32U, 24U};

    uint8_t i;
    uint8_t active = (uint8_t)(frame & 0x07U);

    oled_canvas_clear(framebuffer, 0);
    oled_canvas_draw_circle(framebuffer, 64U, 32U, 14U, 1U);

    for (i = 0U; i < 8U; ++i) {
        uint8_t on = (i == active) ? 1U : 0U;
        oled_canvas_fill_rect(framebuffer,
                              (uint8_t)(spinner_x[i] - 2U),
                              (uint8_t)(spinner_y[i] - 2U),
                              5U,
                              5U,
                              on);
    }
}

void oled_canvas_render_responding(uint8_t *framebuffer, uint16_t frame)
{
    uint8_t mouth_open = ((frame & 0x01U) == 0U) ? 1U : 0U;

    oled_canvas_clear(framebuffer, 0);
    oled_canvas_draw_circle(framebuffer, 64U, 32U, 22U, 1U);
    oled_canvas_fill_rect(framebuffer, 54U, 24U, 6U, 6U, 1U);
    oled_canvas_fill_rect(framebuffer, 68U, 24U, 6U, 6U, 1U);

    if (mouth_open) {
        oled_canvas_fill_rect(framebuffer, 57U, 39U, 14U, 6U, 1U);
    } else {
        oled_canvas_draw_line(framebuffer, 56U, 43U, 72U, 43U, 1U);
    }
}

void oled_canvas_render_error(uint8_t *framebuffer)
{
    oled_canvas_clear(framebuffer, 0);
    oled_canvas_draw_rect(framebuffer, 20U, 8U, 88U, 48U, 1U);
    oled_canvas_draw_line(framebuffer, 28U, 16U, 99U, 47U, 1U);
    oled_canvas_draw_line(framebuffer, 99U, 16U, 28U, 47U, 1U);
}

static void oled_canvas_draw_line(uint8_t *framebuffer,
                                  uint8_t x0,
                                  uint8_t y0,
                                  uint8_t x1,
                                  uint8_t y1,
                                  uint8_t on)
{
    int dx = (x1 > x0) ? (int)(x1 - x0) : (int)(x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? -(int)(y1 - y0) : -(int)(y0 - y1);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int x = (int)x0;
    int y = (int)y0;

    while (1) {
        oled_canvas_set_pixel(framebuffer, (uint8_t)x, (uint8_t)y, on);
        if (x == (int)x1 && y == (int)y1) break;
        {
            int e2 = err << 1;
            if (e2 >= dy) {
                err += dy;
                x += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y += sy;
            }
        }
    }
}

static void oled_canvas_draw_rect(uint8_t *framebuffer,
                                  uint8_t x,
                                  uint8_t y,
                                  uint8_t w,
                                  uint8_t h,
                                  uint8_t on)
{
    uint8_t x2 = (uint8_t)(x + w - 1U);
    uint8_t y2 = (uint8_t)(y + h - 1U);

    if (w == 0U || h == 0U) return;
    oled_canvas_draw_line(framebuffer, x, y, x2, y, on);
    oled_canvas_draw_line(framebuffer, x, y2, x2, y2, on);
    oled_canvas_draw_line(framebuffer, x, y, x, y2, on);
    oled_canvas_draw_line(framebuffer, x2, y, x2, y2, on);
}

static void oled_canvas_fill_rect(uint8_t *framebuffer,
                                  uint8_t x,
                                  uint8_t y,
                                  uint8_t w,
                                  uint8_t h,
                                  uint8_t on)
{
    uint8_t yy;
    if (w == 0U || h == 0U) return;

    for (yy = y; yy < (uint8_t)(y + h); ++yy) {
        oled_canvas_draw_line(framebuffer, x, yy, (uint8_t)(x + w - 1U), yy, on);
    }
}

static void oled_canvas_draw_circle(uint8_t *framebuffer,
                                    uint8_t cx,
                                    uint8_t cy,
                                    uint8_t radius,
                                    uint8_t on)
{
    int x = (int)radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        oled_canvas_set_pixel(framebuffer, (uint8_t)(cx + x), (uint8_t)(cy + y), on);
        oled_canvas_set_pixel(framebuffer, (uint8_t)(cx + y), (uint8_t)(cy + x), on);
        oled_canvas_set_pixel(framebuffer, (uint8_t)(cx - y), (uint8_t)(cy + x), on);
        oled_canvas_set_pixel(framebuffer, (uint8_t)(cx - x), (uint8_t)(cy + y), on);
        oled_canvas_set_pixel(framebuffer, (uint8_t)(cx - x), (uint8_t)(cy - y), on);
        oled_canvas_set_pixel(framebuffer, (uint8_t)(cx - y), (uint8_t)(cy - x), on);
        oled_canvas_set_pixel(framebuffer, (uint8_t)(cx + y), (uint8_t)(cy - x), on);
        oled_canvas_set_pixel(framebuffer, (uint8_t)(cx + x), (uint8_t)(cy - y), on);

        if (err <= 0) {
            ++y;
            err += (y << 1) + 1;
        }
        if (err > 0) {
            --x;
            err -= (x << 1) + 1;
        }
    }
}
