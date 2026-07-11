#ifndef GFX_FONT_H
#define GFX_FONT_H

#include <stdint.h>

#define FONT_WIDTH   8
#define FONT_HEIGHT  16
#define FONT_ASCII_FIRST  0x20   /* space */
#define FONT_ASCII_LAST   0x7E   /* ~ */
#define FONT_GLYPH_BYTES  FONT_HEIGHT   /* 16 bytes per glyph */

/* Render a NUL-terminated string at (x,y) in the given colour.
 * Monospace 8×16 bitmap font — each character advances x by 8.
 * Supports newline (\n): moves to start_x and advances y by 16.
 * Only ASCII printable characters (0x20–0x7E) are rendered;
 * characters outside this range are silently skipped. */
void draw_string(uint32_t x, uint32_t y, const char *str,
                 uint32_t fg_color);

#endif
