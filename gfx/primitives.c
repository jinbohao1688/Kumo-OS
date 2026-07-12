#include "primitives.h"
#include "../mm/multiboot.h"      /* g_framebuffer */

/* ── make_color: pack R,G,B (0-255) into a hardware pixel value ── */

uint32_t make_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t pixel = 0;

    /* Shift each channel down if the hardware has fewer than 8 bits,
     * then shift left into its field position. */
    pixel |= ((uint32_t)(r >> (8 - g_framebuffer.red_mask))   << g_framebuffer.red_pos);
    pixel |= ((uint32_t)(g >> (8 - g_framebuffer.green_mask)) << g_framebuffer.green_pos);
    pixel |= ((uint32_t)(b >> (8 - g_framebuffer.blue_mask))  << g_framebuffer.blue_pos);

    return pixel;
}

/* ── put_pixel: the single pixel-writing path (bounds-checked) ── */

void put_pixel(uint32_t x, uint32_t y, uint32_t packed_color)
{
    if (x >= g_framebuffer.width || y >= g_framebuffer.height)
        return;

    uint8_t *fb = (uint8_t *)g_framebuffer.addr;
    uint32_t off = y * g_framebuffer.pitch + x * (g_framebuffer.bpp / 8);

    switch (g_framebuffer.bpp) {
    case 32:
        *(uint32_t *)(fb + off) = packed_color;
        break;
    case 24:
        fb[off]     = (uint8_t)(packed_color);
        fb[off + 1] = (uint8_t)(packed_color >> 8);
        fb[off + 2] = (uint8_t)(packed_color >> 16);
        break;
    case 16:
        *(uint16_t *)(fb + off) = (uint16_t)packed_color;
        break;
    default:
        break;   /* unsupported bpp — silently skip */
    }
}

/* ── get_pixel: read a single pixel from the framebuffer ── */

uint32_t get_pixel(uint32_t x, uint32_t y)
{
    if (x >= g_framebuffer.width || y >= g_framebuffer.height)
        return 0;

    uint8_t *fb = (uint8_t *)g_framebuffer.addr;
    uint32_t off = y * g_framebuffer.pitch + x * (g_framebuffer.bpp / 8);

    switch (g_framebuffer.bpp) {
    case 32:
        return *(uint32_t *)(fb + off);
    case 24:
        return (uint32_t)fb[off]
             | ((uint32_t)fb[off + 1] << 8)
             | ((uint32_t)fb[off + 2] << 16);
    case 16:
        return (uint32_t)*(uint16_t *)(fb + off);
    default:
        return 0;
    }
}

/* ── Bresenham line algorithm ── */

void draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
               uint32_t packed_color)
{
    int32_t dx  = (int32_t)x1 - (int32_t)x0;
    int32_t dy  = (int32_t)y1 - (int32_t)y0;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t ady = dy < 0 ? -dy : dy;
    int32_t sx  = dx < 0 ? -1 : (dx > 0 ? 1 : 0);
    int32_t sy  = dy < 0 ? -1 : (dy > 0 ? 1 : 0);

    if (adx >= ady) {
        /* Shallow slope — step along x */
        int32_t err = adx / 2;
        int32_t y = (int32_t)y0;
        for (int32_t x = (int32_t)x0; ; x += sx) {
            if (x < 0 || (uint32_t)x >= g_framebuffer.width ||
                y < 0 || (uint32_t)y >= g_framebuffer.height)
                break;
            put_pixel((uint32_t)x, (uint32_t)y, packed_color);
            if (x == (int32_t)x1) break;
            err -= ady;
            if (err < 0) { err += adx; y += sy; }
        }
    } else {
        /* Steep slope — step along y */
        int32_t err = ady / 2;
        int32_t x = (int32_t)x0;
        for (int32_t y = (int32_t)y0; ; y += sy) {
            if (x < 0 || (uint32_t)x >= g_framebuffer.width ||
                y < 0 || (uint32_t)y >= g_framebuffer.height)
                break;
            put_pixel((uint32_t)x, (uint32_t)y, packed_color);
            if (y == (int32_t)y1) break;
            err -= adx;
            if (err < 0) { err += ady; x += sx; }
        }
    }
}

/* ── Hollow rectangle ── */

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
               uint32_t packed_color)
{
    uint32_t x2 = x + w;
    uint32_t y2 = y + h;

    draw_line(x,  y,  x2, y,  packed_color);   /* top    */
    draw_line(x2, y,  x2, y2, packed_color);   /* right  */
    draw_line(x2, y2, x,  y2, packed_color);   /* bottom */
    draw_line(x,  y2, x,  y,  packed_color);   /* left   */
}

/* ── Solid (filled) rectangle ── */

void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
               uint32_t packed_color)
{
    /* Clip to framebuffer boundaries */
    if (x >= g_framebuffer.width || y >= g_framebuffer.height)
        return;
    uint32_t x2 = x + w;
    uint32_t y2 = y + h;
    if (x2 > g_framebuffer.width)  x2 = g_framebuffer.width;
    if (y2 > g_framebuffer.height) y2 = g_framebuffer.height;

    uint8_t *fb = (uint8_t *)g_framebuffer.addr;
    uint32_t bpp_bytes = g_framebuffer.bpp / 8;

    for (uint32_t row = y; row < y2; row++) {
        uint8_t *line = fb + row * g_framebuffer.pitch + x * bpp_bytes;
        for (uint32_t col = x; col < x2; col++) {
            switch (g_framebuffer.bpp) {
            case 32:
                *(uint32_t *)(line) = packed_color;
                break;
            case 24:
                line[0] = (uint8_t)(packed_color);
                line[1] = (uint8_t)(packed_color >> 8);
                line[2] = (uint8_t)(packed_color >> 16);
                break;
            case 16:
                *(uint16_t *)(line) = (uint16_t)packed_color;
                break;
            default:
                break;
            }
            line += bpp_bytes;
        }
    }
}
