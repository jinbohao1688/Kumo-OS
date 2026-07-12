#ifndef GFX_PRIMITIVES_H
#define GFX_PRIMITIVES_H

#include <stdint.h>

/* Pack logical R,G,B (0-255 each) into a hardware pixel value using the
 * framebuffer's color_info fields (red/green/blue position + mask_size).
 * Must be called after multiboot_parse has filled g_framebuffer. */
uint32_t make_color(uint8_t r, uint8_t g, uint8_t b);

/* Write a single pixel.  Bounds-checked — out-of-range pixels are silently
 * dropped (no #PF, no framebuffer corruption). */
void put_pixel(uint32_t x, uint32_t y, uint32_t packed_color);

/* Read a single pixel from the framebuffer.  Returns 0 for out-of-range. */
uint32_t get_pixel(uint32_t x, uint32_t y);

/* Bresenham line algorithm (integer-only). */
void draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
               uint32_t packed_color);

/* Hollow rectangle — four line segments. */
void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
               uint32_t packed_color);

/* Solid (filled) rectangle. */
void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
               uint32_t packed_color);

#endif
