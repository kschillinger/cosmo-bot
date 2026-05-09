/*
 * oled_display.h — 128x128 monochrome OLED driver (SSD1306 / SH1106) over SPI
 *
 * Target:   STM32L476 @ 80 MHz, SPI1 @ 10 MHz
 * Project:  cosmo-bot — conversational chatbot, Phase 4 (display)
 *
 * Layered design (matches spec):
 *   Layer 2 — Low-level display commands (oled_write_command/data)
 *   Layer 3 — Framebuffer management (clear, set_pixel, update_display)
 *   Layer 4 — Graphics primitives (lines, rects, circles, text, bitmaps)
 *   Layer 5 — High-level routines (display states, animations)
 *
 * Memory:
 *   Framebuffer:   2048 B (RAM, static)
 *   Code:          ~6 kB (Flash, -Os)
 *   Font 5x7:      ~480 B (Flash, const)
 */

#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdint.h>
#include <stddef.h>
#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Compile-time configuration                                                */
/* ========================================================================== */

/* Pick exactly one controller. Define on the compiler command line, or here. */
#if !defined(OLED_CONTROLLER_SSD1306) && !defined(OLED_CONTROLLER_SH1106)
#  define OLED_CONTROLLER_SSD1306
#endif

/* Use DMA for the bulk framebuffer flush. Recommended.
 * If undefined, falls back to HAL_SPI_Transmit() (blocking).               */
#ifndef OLED_USE_DMA
#  define OLED_USE_DMA  1
#endif

/* Geometry */
#define OLED_WIDTH                128
#define OLED_HEIGHT               128
#define OLED_PAGES                (OLED_HEIGHT / 8)        /* 16 */
#define OLED_FRAMEBUFFER_SIZE     (OLED_WIDTH * OLED_PAGES) /* 2048 B */

/* Colors */
#define OLED_COLOR_BLACK          0u
#define OLED_COLOR_WHITE          1u
#define OLED_COLOR_INVERT         2u   /* XOR, useful for cursors / overlays */

/* SH1106 has 132-column RAM; only cols 2..129 are visible.
 * We add the 2-column offset transparently when SH1106 is selected.        */
#ifdef OLED_CONTROLLER_SH1106
#  define OLED_COL_OFFSET         2
#else
#  define OLED_COL_OFFSET         0
#endif

/* Default font cell (5x7 glyph rendered in a 6x8 cell — 1 px gap right, 1 px below) */
#define OLED_FONT_W               6
#define OLED_FONT_H               8

/* SPI timeout in ms for blocking ops (small command writes, init).         */
#define OLED_SPI_TIMEOUT_MS       10

/* ========================================================================== */
/*  Types                                                                    */
/* ========================================================================== */

typedef enum {
    OLED_OK            = 0,
    OLED_ERR_PARAM     = -1,
    OLED_ERR_NOT_INIT  = -2,
    OLED_ERR_SPI_FAIL  = -3,
    OLED_ERR_BUSY      = -4
} oled_status_t;

typedef enum {
    DISPLAY_STATE_IDLE = 0,
    DISPLAY_STATE_LISTENING,
    DISPLAY_STATE_PROCESSING,
    DISPLAY_STATE_RESPONDING,
    DISPLAY_STATE_ERROR
} oled_display_state_t;

/* Hardware binding. Caller fills this in and passes to oled_init().        */
typedef struct {
    SPI_HandleTypeDef *hspi;       /* Initialized SPI handle (CubeMX)      */
    GPIO_TypeDef      *cs_port;    /* Chip select   (active LOW)           */
    uint16_t           cs_pin;
    GPIO_TypeDef      *dc_port;    /* Data/Command  (HIGH=data, LOW=cmd)   */
    uint16_t           dc_pin;
    GPIO_TypeDef      *rst_port;   /* Reset         (active LOW)           */
    uint16_t           rst_pin;
} oled_config_t;

/* Framebuffer kept internal to the driver. Expose read access via API.     */
typedef struct {
    uint8_t  buffer[OLED_FRAMEBUFFER_SIZE];
    uint8_t  dirty;       /* set by any draw call, cleared on flush         */
    uint8_t  initialized; /* set by oled_init()                             */
    uint8_t  busy;        /* DMA-in-flight flag                             */
} oled_framebuffer_t;

/* ========================================================================== */
/*  Layer 2 — Init / control                                                 */
/* ========================================================================== */

oled_status_t oled_init             (const oled_config_t *cfg);
oled_status_t oled_set_contrast     (uint8_t contrast);
oled_status_t oled_power_on         (void);
oled_status_t oled_power_off        (void);
oled_status_t oled_invert_display   (uint8_t invert);
uint8_t       oled_is_initialized   (void);
uint8_t       oled_is_busy          (void);

/* ========================================================================== */
/*  Layer 3 — Framebuffer                                                    */
/* ========================================================================== */

void          oled_framebuffer_clear        (void);
void          oled_framebuffer_fill         (void);
void          oled_set_pixel                (int16_t x, int16_t y, uint8_t color);
uint8_t       oled_get_pixel                (int16_t x, int16_t y);
oled_status_t oled_framebuffer_update_display(void);
oled_status_t oled_framebuffer_update_region(uint8_t x1, uint8_t y1,
                                             uint8_t x2, uint8_t y2);
const uint8_t *oled_framebuffer_raw         (void);   /* read-only access  */

/* ========================================================================== */
/*  Layer 4 — Graphics primitives                                            */
/* ========================================================================== */

void          oled_draw_pixel       (int16_t x, int16_t y, uint8_t color);
void          oled_draw_line        (int16_t x0, int16_t y0,
                                     int16_t x1, int16_t y1, uint8_t color);
void          oled_draw_hline       (int16_t x, int16_t y, int16_t w, uint8_t color);
void          oled_draw_vline       (int16_t x, int16_t y, int16_t h, uint8_t color);
void          oled_draw_rect        (int16_t x, int16_t y,
                                     int16_t w, int16_t h,
                                     uint8_t filled, uint8_t color);
void          oled_draw_circle      (int16_t x0, int16_t y0, int16_t r,
                                     uint8_t filled, uint8_t color);
void          oled_draw_char        (int16_t x, int16_t y, char c, uint8_t color);
void          oled_draw_string      (int16_t x, int16_t y, const char *s, uint8_t color);
void          oled_draw_string_wrapped(int16_t x, int16_t y, int16_t max_width,
                                       const char *s, uint8_t color);
void          oled_draw_bitmap      (int16_t x, int16_t y,
                                     int16_t w, int16_t h,
                                     const uint8_t *bitmap, uint8_t color);

uint16_t      oled_measure_string   (const char *s);

/* ========================================================================== */
/*  Layer 5 — High-level routines (defined in oled_animations.c)             */
/* ========================================================================== */

void                  oled_display_set_state(oled_display_state_t state);
oled_display_state_t  oled_display_get_state(void);

void  oled_display_idle        (uint16_t frame);
void  oled_display_listening   (const uint16_t *audio_samples, uint16_t sample_count);
void  oled_display_processing  (uint16_t frame);
void  oled_display_responding  (const char *response_text, uint16_t frame);
void  oled_display_error       (const char *error_message);
void  oled_update_animation_frame(uint16_t frame);

/* ========================================================================== */
/*  ISR hook                                                                 */
/* ========================================================================== */

/* Call this from HAL_SPI_TxCpltCallback() in the user's stm32l4xx_it.c
 * (or from the CubeMX-generated callback) when DMA flush completes.        */
void oled_spi_tx_complete_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* OLED_DISPLAY_H */
