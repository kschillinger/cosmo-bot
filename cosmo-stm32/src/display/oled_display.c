/*
 * oled_display.c — implementation
 *
 * Bresenham line + Midpoint circle, page-mode flush, optional DMA.
 * Coordinate system: (0,0) top-left, x right, y down.
 *
 * Page-mode framebuffer layout:
 *   buffer[page*128 + col]  bit (y % 8)  →  pixel (col, page*8 + (y%8))
 *
 * Bit 0 is the topmost pixel of the page, bit 7 is the bottommost. This is
 * the SSD1306/SH1106 native layout, which means flushing is a straight memcpy
 * from buffer to controller GDDRAM — no per-pixel transposition needed.
 */

#include "oled_display.h"
#include "font_5x7.h"
#include <string.h>

/* ========================================================================== */
/*  Module state                                                              */
/* ========================================================================== */

static oled_config_t      s_cfg;
static oled_framebuffer_t s_fb;

/* ========================================================================== */
/*  Internal helpers                                                          */
/* ========================================================================== */

static inline void cs_low(void)   { HAL_GPIO_WritePin(s_cfg.cs_port,  s_cfg.cs_pin,  GPIO_PIN_RESET); }
static inline void cs_high(void)  { HAL_GPIO_WritePin(s_cfg.cs_port,  s_cfg.cs_pin,  GPIO_PIN_SET);   }
static inline void dc_low(void)   { HAL_GPIO_WritePin(s_cfg.dc_port,  s_cfg.dc_pin,  GPIO_PIN_RESET); }
static inline void dc_high(void)  { HAL_GPIO_WritePin(s_cfg.dc_port,  s_cfg.dc_pin,  GPIO_PIN_SET);   }
static inline void rst_low(void)  { HAL_GPIO_WritePin(s_cfg.rst_port, s_cfg.rst_pin, GPIO_PIN_RESET); }
static inline void rst_high(void) { HAL_GPIO_WritePin(s_cfg.rst_port, s_cfg.rst_pin, GPIO_PIN_SET);   }

static oled_status_t spi_write_blocking(const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef st = HAL_SPI_Transmit(s_cfg.hspi, (uint8_t *)data,
                                            len, OLED_SPI_TIMEOUT_MS);
    return (st == HAL_OK) ? OLED_OK : OLED_ERR_SPI_FAIL;
}

static oled_status_t oled_write_command(uint8_t cmd)
{
    cs_low();
    dc_low();
    oled_status_t s = spi_write_blocking(&cmd, 1);
    cs_high();
    return s;
}

static oled_status_t oled_write_command2(uint8_t cmd, uint8_t arg)
{
    uint8_t buf[2] = { cmd, arg };
    cs_low();
    dc_low();
    oled_status_t s = spi_write_blocking(buf, 2);
    cs_high();
    return s;
}

static oled_status_t oled_write_data(const uint8_t *data, uint16_t len)
{
    cs_low();
    dc_high();
    oled_status_t s = spi_write_blocking(data, len);
    cs_high();
    return s;
}

/* ========================================================================== */
/*  Layer 2 — Init / control                                                  */
/* ========================================================================== */

oled_status_t oled_init(const oled_config_t *cfg)
{
    if (!cfg || !cfg->hspi) return OLED_ERR_PARAM;
    s_cfg = *cfg;

    /* Hardware reset: low ≥ 3 µs, then high ≥ 100 µs.
     * We use generous delays so this works on any board.                  */
    cs_high();
    dc_high();
    rst_high();
    HAL_Delay(1);
    rst_low();
    HAL_Delay(10);
    rst_high();
    HAL_Delay(100);

#ifdef OLED_CONTROLLER_SSD1306
    /* SSD1306 init for 128x128 (multiplex 0x7F).
     * Standard SSD1306 is 128x64; if you have a 128x128 panel using an
     * SSD1306-compatible controller (some are actually SSD1327/SH1107),
     * the multiplex/COM-pin bytes below get you a usable image and
     * SH1106 mode is the safer fallback if you see column shift.        */
    static const uint8_t init_seq[] = {
        0xAE,             /* display OFF                                  */
        0xD5, 0x80,       /* clock divide ratio / oscillator freq         */
        0xA8, 0x7F,       /* multiplex ratio = 127 (128 rows)             */
        0xD3, 0x00,       /* display offset = 0                           */
        0x40,             /* start line = 0                               */
        0x8D, 0x14,       /* charge pump ON (internal)                    */
        0x20, 0x02,       /* page addressing mode                         */
        0xA1,             /* segment remap (X flip)                       */
        0xC8,             /* COM scan dir remap (Y flip)                  */
        0xDA, 0x12,       /* COM pins: alternative, no remap              */
        0x81, 0xCF,       /* contrast                                     */
        0xD9, 0xF1,       /* precharge                                    */
        0xDB, 0x40,       /* VCOMH deselect                               */
        0xA4,             /* output follows RAM (not all-on)              */
        0xA6,             /* normal (non-inverted) display                */
        0x2E,             /* deactivate scroll                            */
        0xAF              /* display ON                                   */
    };
#else /* OLED_CONTROLLER_SH1106 */
    static const uint8_t init_seq[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x7F,
        0xD3, 0x00,
        0x40,
        0xAD, 0x8B,       /* SH1106 charge pump enable (different cmd)    */
        0x33,             /* pump voltage = 9.0V                          */
        /* SH1106 has no horizontal/vertical addressing — page-only.     */
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0xCF,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0x2E,
        0xAF
    };
#endif

    cs_low();
    dc_low();
    oled_status_t s = spi_write_blocking(init_seq, sizeof(init_seq));
    cs_high();
    if (s != OLED_OK) return s;

    /* Blank the framebuffer and push it before declaring init done.       */
    memset(s_fb.buffer, 0x00, OLED_FRAMEBUFFER_SIZE);
    s_fb.dirty       = 0;
    s_fb.busy        = 0;
    s_fb.initialized = 1;

    /* Force a full flush via the public path (which validates state).    */
    s_fb.dirty = 1;
    return oled_framebuffer_update_display();
}

oled_status_t oled_set_contrast(uint8_t contrast)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    return oled_write_command2(0x81, contrast);
}

oled_status_t oled_power_on(void)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    return oled_write_command(0xAF);
}

oled_status_t oled_power_off(void)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    return oled_write_command(0xAE);
}

oled_status_t oled_invert_display(uint8_t invert)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    return oled_write_command(invert ? 0xA7 : 0xA6);
}

uint8_t oled_is_initialized(void) { return s_fb.initialized; }
uint8_t oled_is_busy(void)        { return s_fb.busy;        }

/* ========================================================================== */
/*  Layer 3 — Framebuffer                                                     */
/* ========================================================================== */

void oled_framebuffer_clear(void)
{
    memset(s_fb.buffer, 0x00, OLED_FRAMEBUFFER_SIZE);
    s_fb.dirty = 1;
}

void oled_framebuffer_fill(void)
{
    memset(s_fb.buffer, 0xFF, OLED_FRAMEBUFFER_SIZE);
    s_fb.dirty = 1;
}

void oled_set_pixel(int16_t x, int16_t y, uint8_t color)
{
    if ((unsigned)x >= OLED_WIDTH || (unsigned)y >= OLED_HEIGHT) return;

    const uint16_t idx  = (uint16_t)((y >> 3) * OLED_WIDTH + x);
    const uint8_t  mask = (uint8_t)(1u << (y & 7));

    switch (color) {
        case OLED_COLOR_WHITE:  s_fb.buffer[idx] |=  mask;  break;
        case OLED_COLOR_BLACK:  s_fb.buffer[idx] &= ~mask;  break;
        case OLED_COLOR_INVERT: s_fb.buffer[idx] ^=  mask;  break;
        default: return;
    }
    s_fb.dirty = 1;
}

uint8_t oled_get_pixel(int16_t x, int16_t y)
{
    if ((unsigned)x >= OLED_WIDTH || (unsigned)y >= OLED_HEIGHT) return 0;
    const uint16_t idx  = (uint16_t)((y >> 3) * OLED_WIDTH + x);
    return (s_fb.buffer[idx] >> (y & 7)) & 1u;
}

const uint8_t *oled_framebuffer_raw(void) { return s_fb.buffer; }

/* ---- Flush helpers ------------------------------------------------------- */

static oled_status_t flush_page_range(uint8_t first_page, uint8_t last_page,
                                      uint8_t col_start,  uint8_t col_end)
{
    if (col_start >= OLED_WIDTH || col_end >= OLED_WIDTH || col_start > col_end)
        return OLED_ERR_PARAM;
    if (first_page >= OLED_PAGES || last_page >= OLED_PAGES || first_page > last_page)
        return OLED_ERR_PARAM;

    const uint16_t span = (uint16_t)(col_end - col_start + 1);

    for (uint8_t page = first_page; page <= last_page; ++page) {
        const uint8_t col_phys = (uint8_t)(col_start + OLED_COL_OFFSET);
        uint8_t cmd[3] = {
            (uint8_t)(0xB0 | page),               /* set page              */
            (uint8_t)(0x00 | (col_phys & 0x0F)),  /* col low nibble        */
            (uint8_t)(0x10 | (col_phys >> 4))     /* col high nibble       */
        };
        cs_low();
        dc_low();
        if (spi_write_blocking(cmd, 3) != OLED_OK) { cs_high(); return OLED_ERR_SPI_FAIL; }
        cs_high();

        cs_low();
        dc_high();
        oled_status_t s = spi_write_blocking(&s_fb.buffer[page * OLED_WIDTH + col_start], span);
        cs_high();
        if (s != OLED_OK) return s;
    }
    return OLED_OK;
}

/* DMA flush — pushes the entire framebuffer page-by-page. We submit page 0
 * synchronously to set the address, then the data via HAL_SPI_Transmit_DMA.
 * The completion ISR drives the next page. For simplicity here we expose
 * a synchronous-looking call that uses DMA per page and waits.            */
#if OLED_USE_DMA
static volatile uint8_t s_dma_done;

static oled_status_t flush_full_dma(void)
{
    s_fb.busy = 1;

    for (uint8_t page = 0; page < OLED_PAGES; ++page) {
        uint8_t cmd[3] = {
            (uint8_t)(0xB0 | page),
            (uint8_t)(0x00 | (OLED_COL_OFFSET & 0x0F)),
            (uint8_t)(0x10 | (OLED_COL_OFFSET >> 4))
        };
        cs_low();
        dc_low();
        if (spi_write_blocking(cmd, 3) != OLED_OK) { cs_high(); s_fb.busy = 0; return OLED_ERR_SPI_FAIL; }
        cs_high();

        cs_low();
        dc_high();
        s_dma_done = 0;
        if (HAL_SPI_Transmit_DMA(s_cfg.hspi,
                                 &s_fb.buffer[page * OLED_WIDTH],
                                 OLED_WIDTH) != HAL_OK) {
            cs_high();
            s_fb.busy = 0;
            return OLED_ERR_SPI_FAIL;
        }
        /* Wait for DMA. Safe even without ISR plumbing — HAL state is OK. */
        uint32_t t0 = HAL_GetTick();
        while (HAL_SPI_GetState(s_cfg.hspi) != HAL_SPI_STATE_READY) {
            if (HAL_GetTick() - t0 > 50) { cs_high(); s_fb.busy = 0; return OLED_ERR_SPI_FAIL; }
        }
        cs_high();
    }

    s_fb.busy = 0;
    return OLED_OK;
}
#endif

oled_status_t oled_framebuffer_update_display(void)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    if (!s_fb.dirty)       return OLED_OK;

#if OLED_USE_DMA
    oled_status_t s = flush_full_dma();
#else
    oled_status_t s = flush_page_range(0, OLED_PAGES - 1, 0, OLED_WIDTH - 1);
#endif
    if (s == OLED_OK) s_fb.dirty = 0;
    return s;
}

oled_status_t oled_framebuffer_update_region(uint8_t x1, uint8_t y1,
                                             uint8_t x2, uint8_t y2)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    /* Page-mode means the smallest vertical unit is a page (8 rows).
     * Round y1 down and y2 up to page boundaries.                          */
    const uint8_t p1 = (uint8_t)(y1 >> 3);
    const uint8_t p2 = (uint8_t)(y2 >> 3);
    return flush_page_range(p1, p2, x1, x2);
}

/* ISR — keep for future fully-async path. Currently unused but exposed.   */
void oled_spi_tx_complete_isr(void)
{
#if OLED_USE_DMA
    s_dma_done = 1;
#endif
}

/* ========================================================================== */
/*  Layer 4 — Graphics primitives                                             */
/* ========================================================================== */

void oled_draw_pixel(int16_t x, int16_t y, uint8_t color)
{
    oled_set_pixel(x, y, color);
}

void oled_draw_hline(int16_t x, int16_t y, int16_t w, uint8_t color)
{
    if (w <= 0) return;
    if (y < 0 || y >= OLED_HEIGHT) return;
    if (x < 0)               { w += x; x = 0; }
    if (x + w > OLED_WIDTH)  { w = OLED_WIDTH - x; }
    if (w <= 0) return;
    for (int16_t i = 0; i < w; ++i) oled_set_pixel((int16_t)(x + i), y, color);
}

void oled_draw_vline(int16_t x, int16_t y, int16_t h, uint8_t color)
{
    if (h <= 0) return;
    if (x < 0 || x >= OLED_WIDTH) return;
    if (y < 0)               { h += y; y = 0; }
    if (y + h > OLED_HEIGHT) { h = OLED_HEIGHT - y; }
    if (h <= 0) return;
    for (int16_t i = 0; i < h; ++i) oled_set_pixel(x, (int16_t)(y + i), color);
}

/* Bresenham line — integer-only, handles all 8 octants. */
void oled_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color)
{
    int16_t dx =  (int16_t)((x1 > x0) ? (x1 - x0) : (x0 - x1));
    int16_t dy = -(int16_t)((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (int16_t)(dx + dy);

    for (;;) {
        oled_set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = (int16_t)(err << 1);
        if (e2 >= dy) { err = (int16_t)(err + dy); x0 = (int16_t)(x0 + sx); }
        if (e2 <= dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
    }
}

void oled_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint8_t filled, uint8_t color)
{
    if (w <= 0 || h <= 0) return;
    if (filled) {
        for (int16_t row = 0; row < h; ++row) {
            oled_draw_hline(x, (int16_t)(y + row), w, color);
        }
    } else {
        oled_draw_hline(x,                (int16_t)y,             w, color);
        oled_draw_hline(x,                (int16_t)(y + h - 1),   w, color);
        oled_draw_vline(x,                (int16_t)y,             h, color);
        oled_draw_vline((int16_t)(x+w-1), (int16_t)y,             h, color);
    }
}

/* Midpoint circle — Bresenham's variant. 8-fold symmetry, integer-only. */
static void plot_circle_points(int16_t cx, int16_t cy,
                               int16_t x,  int16_t y, uint8_t color)
{
    oled_set_pixel((int16_t)(cx + x), (int16_t)(cy + y), color);
    oled_set_pixel((int16_t)(cx - x), (int16_t)(cy + y), color);
    oled_set_pixel((int16_t)(cx + x), (int16_t)(cy - y), color);
    oled_set_pixel((int16_t)(cx - x), (int16_t)(cy - y), color);
    oled_set_pixel((int16_t)(cx + y), (int16_t)(cy + x), color);
    oled_set_pixel((int16_t)(cx - y), (int16_t)(cy + x), color);
    oled_set_pixel((int16_t)(cx + y), (int16_t)(cy - x), color);
    oled_set_pixel((int16_t)(cx - y), (int16_t)(cy - x), color);
}

void oled_draw_circle(int16_t cx, int16_t cy, int16_t r,
                      uint8_t filled, uint8_t color)
{
    if (r <= 0) { oled_set_pixel(cx, cy, color); return; }

    int16_t x = 0;
    int16_t y = r;
    int16_t d = (int16_t)(1 - r);

    while (x <= y) {
        if (filled) {
            /* Two horizontal scanlines per step covers the disk.        */
            oled_draw_hline((int16_t)(cx - x), (int16_t)(cy + y), (int16_t)(2*x + 1), color);
            oled_draw_hline((int16_t)(cx - x), (int16_t)(cy - y), (int16_t)(2*x + 1), color);
            oled_draw_hline((int16_t)(cx - y), (int16_t)(cy + x), (int16_t)(2*y + 1), color);
            oled_draw_hline((int16_t)(cx - y), (int16_t)(cy - x), (int16_t)(2*y + 1), color);
        } else {
            plot_circle_points(cx, cy, x, y, color);
        }

        if (d < 0) {
            d = (int16_t)(d + 2*x + 3);
        } else {
            d = (int16_t)(d + 2*(x - y) + 5);
            --y;
        }
        ++x;
    }
}

/* Text rendering — column-major 5x7 font, each char drawn into a 6x8 cell. */
void oled_draw_char(int16_t x, int16_t y, char c, uint8_t color)
{
    if (c < FONT_5X7_FIRST_CHAR || c > FONT_5X7_LAST_CHAR) c = '?';
    const uint8_t *glyph = font_5x7_glyph((uint8_t)c);

    for (uint8_t col = 0; col < 5; ++col) {
        uint8_t bits = glyph[col];
        for (uint8_t row = 0; row < 7; ++row) {
            if (bits & (1u << row)) {
                oled_set_pixel((int16_t)(x + col), (int16_t)(y + row), color);
            } else if (color == OLED_COLOR_INVERT) {
                /* invert mode also flips background — caller's intent.    */
            }
        }
    }
    /* The 6th column and 8th row stay blank → natural inter-glyph gap.    */
}

void oled_draw_string(int16_t x, int16_t y, const char *s, uint8_t color)
{
    if (!s) return;
    int16_t cx = x;
    while (*s) {
        if (*s == '\n')      { cx = x;  y = (int16_t)(y + OLED_FONT_H); }
        else if (*s == '\r') { cx = x; }
        else                 { oled_draw_char(cx, y, *s, color); cx = (int16_t)(cx + OLED_FONT_W); }
        ++s;
    }
}

uint16_t oled_measure_string(const char *s)
{
    if (!s) return 0;
    uint16_t w = 0, line = 0;
    while (*s) {
        if (*s == '\n')      { if (line > w) w = line; line = 0; }
        else if (*s != '\r') { line += OLED_FONT_W; }
        ++s;
    }
    return (line > w) ? line : w;
}

/* Word-wrap on whitespace. Words longer than max_width are hard-broken. */
void oled_draw_string_wrapped(int16_t x, int16_t y, int16_t max_width,
                              const char *s, uint8_t color)
{
    if (!s || max_width < OLED_FONT_W) return;

    int16_t cx = x;
    int16_t cy = y;
    const int16_t right = (int16_t)(x + max_width);

    while (*s) {
        /* Skip leading run of spaces at the start of a wrapped line.      */
        while (*s == ' ' && cx == x) ++s;
        if (!*s) break;

        /* Find the next word boundary.                                    */
        const char *word = s;
        while (*s && *s != ' ' && *s != '\n') ++s;
        const int16_t word_px = (int16_t)((s - word) * OLED_FONT_W);

        if (cx != x && cx + word_px > right) {
            /* Wrap before the word.                                       */
            cx = x;
            cy = (int16_t)(cy + OLED_FONT_H);
        }

        if (word_px > max_width) {
            /* Hard-break: render until edge, wrap, repeat.                */
            for (const char *p = word; p < s; ++p) {
                if (cx + OLED_FONT_W > right) {
                    cx = x;
                    cy = (int16_t)(cy + OLED_FONT_H);
                }
                oled_draw_char(cx, cy, *p, color);
                cx = (int16_t)(cx + OLED_FONT_W);
            }
        } else {
            for (const char *p = word; p < s; ++p) {
                oled_draw_char(cx, cy, *p, color);
                cx = (int16_t)(cx + OLED_FONT_W);
            }
        }

        if (*s == ' ') {
            if (cx + OLED_FONT_W <= right) {
                cx = (int16_t)(cx + OLED_FONT_W);
            }
            ++s;
        } else if (*s == '\n') {
            cx = x;
            cy = (int16_t)(cy + OLED_FONT_H);
            ++s;
        }
    }
}

/* Bitmap layout: row-major, 1 bpp, MSB first per byte, padded to byte/row. */
void oled_draw_bitmap(int16_t x, int16_t y, int16_t w, int16_t h,
                      const uint8_t *bitmap, uint8_t color)
{
    if (!bitmap || w <= 0 || h <= 0) return;
    const int16_t row_bytes = (int16_t)((w + 7) >> 3);

    for (int16_t row = 0; row < h; ++row) {
        for (int16_t col = 0; col < w; ++col) {
            const uint8_t byte = bitmap[row * row_bytes + (col >> 3)];
            if (byte & (uint8_t)(0x80u >> (col & 7))) {
                oled_set_pixel((int16_t)(x + col), (int16_t)(y + row), color);
            }
        }
    }
}
