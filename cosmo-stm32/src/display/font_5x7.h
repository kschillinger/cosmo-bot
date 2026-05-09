/*
 * font_5x7.h — compact ASCII bitmap font for the OLED driver
 *
 * Format:  each glyph is 5 bytes. Each byte = one column.
 *          Bit 0 is the top pixel, bit 6 is the bottom pixel of a 7-row glyph
 *          (bit 7 unused). This matches the SSD1306 page-mode native layout,
 *          so rendering is just a bit-shift + set-pixel per column.
 *
 * Coverage: ASCII 0x20..0x7E (printable). 0x7F shown as a solid block.
 * Storage:  96 glyphs * 5 bytes = 480 bytes in Flash (const).
 */

#ifndef FONT_5X7_H
#define FONT_5X7_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_5X7_FIRST_CHAR  0x20  /* space  */
#define FONT_5X7_LAST_CHAR   0x7F  /* DEL    */
#define FONT_5X7_GLYPH_BYTES 5
#define FONT_5X7_HEIGHT      7
#define FONT_5X7_WIDTH       5

/* Returns a pointer to the 5-byte column data for character c.
 * Out-of-range characters are clamped to '?'.                              */
const uint8_t *font_5x7_glyph(uint8_t c);

#ifdef __cplusplus
}
#endif

#endif /* FONT_5X7_H */
