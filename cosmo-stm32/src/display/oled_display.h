/*
 * oled_display.h — 128x64 monochrome OLED driver (I2C, SSD1306).
 *
 * Variant of the SPI driver on branch `screendrivers`, retargeted for a
 * 4-pin I2C panel. Layered as before:
 *
 *   Layer 2  ─  I2C primitives, init/contrast/power
 *   Layer 3  ─  1 KB framebuffer, full + region flush
 *   Layer 4  ─  graphics primitives (line, circle, rect, text, bitmap)
 *   Layer 5  ─  dialogue-state animations (oled_animations.c)
 *
 * Defaults to a 0.96" 128x64 SSD1306 module at 7-bit address 0x3C.
 * To use a 128x128 SH1107 instead, define OLED_PANEL_128x128_SH1107
 * before including this file (or globally in platformio.ini) — the
 * dimensions and init sequence adapt; nothing else changes.
 */

#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "stm32l4xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Panel configuration                                                       */
/* ========================================================================== */

#ifdef OLED_PANEL_128x128_SH1107
#  define OLED_WIDTH         128
#  define OLED_HEIGHT        128
#  define OLED_PAGES          16
#  define OLED_COL_OFFSET      0
#  define OLED_CONTROLLER_SH1107
#else
   /* Default: 128x64 SSD1306 (the typical 4-pin I2C "0.96 inch" module).   */
#  define OLED_WIDTH         128
#  define OLED_HEIGHT         64
#  define OLED_PAGES           8
#  define OLED_COL_OFFSET      0
#  define OLED_CONTROLLER_SSD1306
#endif

#define OLED_FRAMEBUFFER_SIZE  (OLED_WIDTH * OLED_PAGES)

/* Font metrics — 5x7 in a 6x8 cell. */
#define OLED_FONT_W            6
#define OLED_FONT_H            8

/* I2C address — 7-bit form for documentation; HAL wants the 8-bit (shifted)
 * form: 0x3C << 1 = 0x78.                                                  */
#define OLED_I2C_ADDR_7BIT     0x3C
#define OLED_I2C_ADDR_8BIT     (OLED_I2C_ADDR_7BIT << 1)   /* 0x78 */

#define OLED_I2C_TIMEOUT_MS    100

/* ========================================================================== */
/*  Types                                                                     */
/* ========================================================================== */

typedef enum {
    OLED_OK              = 0,
    OLED_ERR_PARAM       = -1,
    OLED_ERR_NOT_INIT    = -2,
    OLED_ERR_I2C_FAIL    = -3,
    OLED_ERR_BUSY        = -4
} oled_status_t;

typedef enum {
    OLED_COLOR_BLACK     = 0,
    OLED_COLOR_WHITE     = 1,
    OLED_COLOR_INVERT    = 2
} oled_color_t;

typedef struct {
    I2C_HandleTypeDef *hi2c;       /* HAL I2C handle (e.g. &hi2c1)         */
    uint8_t            address;    /* 8-bit HAL address (0x78 default)     */
} oled_config_t;

typedef struct {
    uint8_t  buffer[OLED_FRAMEBUFFER_SIZE];
    uint8_t  dirty;
    uint8_t  busy;
    uint8_t  initialized;
} oled_framebuffer_t;

/* Layer-5 state machine. */
typedef enum {
    DISPLAY_STATE_IDLE       = 0,
    DISPLAY_STATE_LISTENING,
    DISPLAY_STATE_PROCESSING,
    DISPLAY_STATE_RESPONDING,
    DISPLAY_STATE_ERROR
} oled_display_state_t;

/* ========================================================================== */
/*  Layer 2 — init / control                                                  */
/* ========================================================================== */

oled_status_t  oled_init                (const oled_config_t *cfg);
oled_status_t  oled_set_contrast        (uint8_t contrast);
oled_status_t  oled_power_on            (void);
oled_status_t  oled_power_off           (void);
oled_status_t  oled_invert_display      (uint8_t invert);
uint8_t        oled_is_initialized      (void);
uint8_t        oled_is_busy             (void);

/* ========================================================================== */
/*  Layer 3 — framebuffer                                                     */
/* ========================================================================== */

void                  oled_framebuffer_clear           (void);
void                  oled_framebuffer_fill            (void);
void                  oled_set_pixel                   (int16_t x, int16_t y, uint8_t color);
uint8_t               oled_get_pixel                   (int16_t x, int16_t y);
const uint8_t        *oled_framebuffer_raw             (void);
oled_status_t         oled_framebuffer_update_display  (void);
oled_status_t         oled_framebuffer_update_region   (uint8_t x1, uint8_t y1,
                                                        uint8_t x2, uint8_t y2);

/* ========================================================================== */
/*  Layer 4 — primitives                                                      */
/* ========================================================================== */

void     oled_draw_pixel           (int16_t x, int16_t y, uint8_t color);
void     oled_draw_hline           (int16_t x, int16_t y, int16_t w, uint8_t color);
void     oled_draw_vline           (int16_t x, int16_t y, int16_t h, uint8_t color);
void     oled_draw_line            (int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
void     oled_draw_rect            (int16_t x, int16_t y, int16_t w, int16_t h,
                                    uint8_t filled, uint8_t color);
void     oled_draw_circle          (int16_t cx, int16_t cy, int16_t r,
                                    uint8_t filled, uint8_t color);
void     oled_draw_char            (int16_t x, int16_t y, char c, uint8_t color);
void     oled_draw_string          (int16_t x, int16_t y, const char *s, uint8_t color);
uint16_t oled_measure_string       (const char *s);
void     oled_draw_string_wrapped  (int16_t x, int16_t y, int16_t max_width,
                                    const char *s, uint8_t color);
void     oled_draw_bitmap          (int16_t x, int16_t y, int16_t w, int16_t h,
                                    const uint8_t *bitmap, uint8_t color);

/* ========================================================================== */
/*  Layer 5 — animations                                                      */
/* ========================================================================== */

void                  oled_display_set_state          (oled_display_state_t state);
oled_display_state_t  oled_display_get_state          (void);

void  oled_display_idle         (uint16_t frame);
void  oled_display_listening    (const uint16_t *audio_samples, uint16_t sample_count);
void  oled_display_processing   (uint16_t frame);
void  oled_display_responding   (const char *response_text, uint16_t frame);
void  oled_display_error        (const char *error_message);
void  oled_update_animation_frame (uint16_t frame);

#ifdef __cplusplus
}
#endif

#endif /* OLED_DISPLAY_H */
