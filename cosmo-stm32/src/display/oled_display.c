/*
 * oled_display.c — I2C implementation for 128x64 SSD1306 (default).
 *
 * Page-mode framebuffer layout (same as the SPI variant):
 *   buffer[page*OLED_WIDTH + col]  bit (y % 8)  →  pixel (col, page*8 + (y%8))
 *
 * I2C transport: HAL_I2C_Mem_Write with the OLED's "control byte" as the
 * 1-byte memory address:
 *   - 0x00  →  payload is a stream of commands
 *   - 0x40  →  payload is a stream of pixel data
 *
 * Full framebuffer flush uses SSD1306 horizontal addressing mode: one
 * command burst sets the column/page range, then a single data burst
 * dumps the entire framebuffer. At 400 kHz this is ~26 ms per full flush.
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
/*  Internal — I2C primitives                                                 */
/* ========================================================================== */

static oled_status_t i2c_write_cmd_byte(uint8_t cmd)
{
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(
        s_cfg.hi2c, s_cfg.address,
        0x00, I2C_MEMADD_SIZE_8BIT,
        &cmd, 1, OLED_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? OLED_OK : OLED_ERR_I2C_FAIL;
}

static oled_status_t i2c_write_cmd_list(const uint8_t *cmds, uint16_t len)
{
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(
        s_cfg.hi2c, s_cfg.address,
        0x00, I2C_MEMADD_SIZE_8BIT,
        (uint8_t *)cmds, len, OLED_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? OLED_OK : OLED_ERR_I2C_FAIL;
}

static oled_status_t i2c_write_data(const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(
        s_cfg.hi2c, s_cfg.address,
        0x40, I2C_MEMADD_SIZE_8BIT,
        (uint8_t *)data, len, OLED_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? OLED_OK : OLED_ERR_I2C_FAIL;
}

/* ========================================================================== */
/*  Layer 2 — init / control                                                  */
/* ========================================================================== */

oled_status_t oled_init(const oled_config_t *cfg)
{
    if (!cfg || !cfg->hi2c) return OLED_ERR_PARAM;
    s_cfg = *cfg;
    if (s_cfg.address == 0) s_cfg.address = OLED_I2C_ADDR_8BIT;

    /* Small power-on settle before talking. */
    HAL_Delay(50);

    /* Confirm the panel is on the bus before sending the long init. */
    if (HAL_I2C_IsDeviceReady(s_cfg.hi2c, s_cfg.address, 3, 20) != HAL_OK) {
        return OLED_ERR_I2C_FAIL;
    }

#ifdef OLED_CONTROLLER_SSD1306
    /* SSD1306 128x64 init. */
    static const uint8_t init_seq[] = {
        0xAE,             /* display OFF                                 */
        0xD5, 0x80,       /* clock divide / oscillator                   */
        0xA8, 0x3F,       /* multiplex ratio = 63 (64 rows)              */
        0xD3, 0x00,       /* display offset = 0                          */
        0x40,             /* start line = 0                              */
        0x8D, 0x14,       /* charge pump ON (internal)                   */
        0x20, 0x00,       /* horizontal addressing mode                  */
        0xA1,             /* segment remap (X flip)                      */
        0xC8,             /* COM scan dir remap (Y flip)                 */
        0xDA, 0x12,       /* COM pins: alternative, no remap             */
        0x81, 0xCF,       /* contrast                                    */
        0xD9, 0xF1,       /* precharge                                   */
        0xDB, 0x40,       /* VCOMH deselect                              */
        0xA4,             /* output follows RAM (not all-on)             */
        0xA6,             /* normal (non-inverted) display               */
        0x2E,             /* deactivate scroll                           */
        0xAF              /* display ON                                  */
    };
#elif defined(OLED_CONTROLLER_SH1106)
    /* SH1106 128x64 init. */
    static const uint8_t init_seq[] = {
        0xAE,             /* display OFF                                 */
        0xD5, 0x80,       /* clock divide / oscillator                   */
        0xA8, 0x3F,       /* multiplex ratio = 63 (64 rows)              */
        0xD3, 0x00,       /* display offset = 0                          */
        0x40,             /* start line = 0                              */
        0xAD, 0x8B,       /* DC-DC on (internal)                         */
        0xA1,             /* segment remap (X flip)                      */
        0xC8,             /* COM scan dir remap (Y flip)                 */
        0xDA, 0x12,       /* COM pins: alternative, no remap             */
        0x81, 0x80,       /* contrast                                    */
        0xD9, 0x1F,       /* precharge                                   */
        0xDB, 0x40,       /* VCOMH deselect                              */
        0xA4,             /* output follows RAM (not all-on)             */
        0xA6,             /* normal (non-inverted) display               */
        0xAF              /* display ON                                  */
    };
#else /* OLED_CONTROLLER_SH1107 — 128x128 */
    static const uint8_t init_seq[] = {
        0xAE,
        0xD5, 0x51,
        0xA8, 0x7F,       /* multiplex ratio = 127 (128 rows)            */
        0xD3, 0x00,
        0xDC, 0x00,       /* display start line = 0                      */
        0xAD, 0x8B,       /* charge pump                                 */
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0x80,
        0xD9, 0x22,
        0xDB, 0x35,
        0xA4,
        0xA6,
        0xAF
    };
#endif

    oled_status_t s = i2c_write_cmd_list(init_seq, sizeof(init_seq));
    if (s != OLED_OK) return s;

    /* Blank framebuffer and push. */
    memset(s_fb.buffer, 0x00, OLED_FRAMEBUFFER_SIZE);
    s_fb.dirty       = 0;
    s_fb.busy        = 0;
    s_fb.initialized = 1;
    s_fb.dirty       = 1;
    return oled_framebuffer_update_display();
}

oled_status_t oled_set_contrast(uint8_t contrast)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    uint8_t cmds[2] = { 0x81, contrast };
    return i2c_write_cmd_list(cmds, 2);
}

oled_status_t oled_power_on(void)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    return i2c_write_cmd_byte(0xAF);
}

oled_status_t oled_power_off(void)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    return i2c_write_cmd_byte(0xAE);
}

oled_status_t oled_invert_display(uint8_t invert)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    return i2c_write_cmd_byte(invert ? 0xA7 : 0xA6);
}

uint8_t oled_is_initialized(void) { return s_fb.initialized; }
uint8_t oled_is_busy(void)        { return s_fb.busy;        }

/* ========================================================================== */
/*  Layer 3 — framebuffer                                                     */
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
    const uint16_t idx = (uint16_t)((y >> 3) * OLED_WIDTH + x);
    return (s_fb.buffer[idx] >> (y & 7)) & 1u;
}

const uint8_t *oled_framebuffer_raw(void) { return s_fb.buffer; }

/* ---- Flush helpers ------------------------------------------------------- */

#ifdef OLED_CONTROLLER_SH1106
static oled_status_t sh1106_set_page_col(uint8_t page, uint8_t col)
{
    const uint8_t col_off = (uint8_t)(col + OLED_COL_OFFSET);
    uint8_t cmds[3] = {
        (uint8_t)(0xB0 | (page & 0x0F)),
        (uint8_t)(0x00 | (col_off & 0x0F)),
        (uint8_t)(0x10 | ((col_off >> 4) & 0x0F))
    };
    return i2c_write_cmd_list(cmds, 3);
}
#else
static oled_status_t set_window(uint8_t x1, uint8_t x2, uint8_t p1, uint8_t p2)
{
    uint8_t cmds[6] = {
        0x21, x1, x2,     /* column address range                        */
        0x22, p1, p2      /* page address range                          */
    };
    return i2c_write_cmd_list(cmds, 6);
}
#endif

oled_status_t oled_framebuffer_update_display(void)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    if (!s_fb.dirty)       return OLED_OK;

    s_fb.busy = 1;
#ifdef OLED_CONTROLLER_SH1106
    for (uint8_t p = 0; p < OLED_PAGES; ++p) {
        oled_status_t s = sh1106_set_page_col(p, 0);
        if (s != OLED_OK) { s_fb.busy = 0; return s; }
        s = i2c_write_data(&s_fb.buffer[p * OLED_WIDTH], OLED_WIDTH);
        if (s != OLED_OK) { s_fb.busy = 0; return s; }
    }
    s_fb.dirty = 0;
    s_fb.busy = 0;
    return OLED_OK;
#else
    oled_status_t s = set_window(0, OLED_WIDTH - 1, 0, OLED_PAGES - 1);
    if (s != OLED_OK) { s_fb.busy = 0; return s; }

    s = i2c_write_data(s_fb.buffer, OLED_FRAMEBUFFER_SIZE);
    if (s == OLED_OK) s_fb.dirty = 0;
    s_fb.busy = 0;
    return s;
#endif
}

oled_status_t oled_framebuffer_update_region(uint8_t x1, uint8_t y1,
                                             uint8_t x2, uint8_t y2)
{
    if (!s_fb.initialized) return OLED_ERR_NOT_INIT;
    if (x1 > x2 || y1 > y2) return OLED_ERR_PARAM;
    if (x2 >= OLED_WIDTH || y2 >= OLED_HEIGHT) return OLED_ERR_PARAM;

    const uint8_t  p1   = (uint8_t)(y1 >> 3);
    const uint8_t  p2   = (uint8_t)(y2 >> 3);
    const uint16_t span = (uint16_t)(x2 - x1 + 1);

    /* Send each page as its own (set-window + data) pair — the bytes for
     * a region aren't contiguous in the framebuffer since each page has
     * OLED_WIDTH bytes but we only want `span` of them.                   */
    for (uint8_t p = p1; p <= p2; ++p) {
#ifdef OLED_CONTROLLER_SH1106
        oled_status_t s = sh1106_set_page_col(p, x1);
        if (s != OLED_OK) return s;
        s = i2c_write_data(&s_fb.buffer[p * OLED_WIDTH + x1], span);
        if (s != OLED_OK) return s;
#else
        oled_status_t s = set_window(x1, x2, p, p);
        if (s != OLED_OK) return s;
        s = i2c_write_data(&s_fb.buffer[p * OLED_WIDTH + x1], span);
        if (s != OLED_OK) return s;
#endif
    }
    return OLED_OK;
}

/* ========================================================================== */
/*  Layer 4 — primitives                                                      */
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

/* Midpoint circle — Bresenham's variant. 8-fold symmetry. */
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

void oled_draw_char(int16_t x, int16_t y, char c, uint8_t color)
{
    if (c < FONT_5X7_FIRST_CHAR || c > FONT_5X7_LAST_CHAR) c = '?';
    const uint8_t *glyph = font_5x7_glyph((uint8_t)c);

    for (uint8_t col = 0; col < 5; ++col) {
        uint8_t bits = glyph[col];
        for (uint8_t row = 0; row < 7; ++row) {
            if (bits & (1u << row)) {
                oled_set_pixel((int16_t)(x + col), (int16_t)(y + row), color);
            }
        }
    }
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

void oled_draw_string_wrapped(int16_t x, int16_t y, int16_t max_width,
                              const char *s, uint8_t color)
{
    if (!s || max_width < OLED_FONT_W) return;

    int16_t cx = x;
    int16_t cy = y;
    const int16_t right = (int16_t)(x + max_width);

    while (*s) {
        while (*s == ' ' && cx == x) ++s;
        if (!*s) break;

        const char *word = s;
        while (*s && *s != ' ' && *s != '\n') ++s;
        const int16_t word_px = (int16_t)((s - word) * OLED_FONT_W);

        if (cx != x && cx + word_px > right) {
            cx = x;
            cy = (int16_t)(cy + OLED_FONT_H);
        }

        if (word_px > max_width) {
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
            if (cx + OLED_FONT_W <= right) cx = (int16_t)(cx + OLED_FONT_W);
            ++s;
        } else if (*s == '\n') {
            cx = x;
            cy = (int16_t)(cy + OLED_FONT_H);
            ++s;
        }
    }
}

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
