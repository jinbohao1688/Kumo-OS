#include "window.h"
#include "../gfx/primitives.h"
#include "../gfx/font.h"

void window_draw(const window_t *win)
{
    uint32_t border_color = make_color(0x40, 0x40, 0x40);
    uint32_t title_text_color = make_color(0xFF, 0xFF, 0xFF);

    draw_rect((uint32_t)win->x, (uint32_t)win->y,
              win->w, win->h, border_color);

    fill_rect((uint32_t)(win->x + 1), (uint32_t)(win->y + 1),
              win->w - 2, TITLE_BAR_H, win->title_bar_color);

    draw_string((uint32_t)(win->x + 4), (uint32_t)(win->y + 2),
                win->title, title_text_color);

    uint32_t body_y = (uint32_t)(win->y + TITLE_BAR_H + 1);
    uint32_t body_h = win->h - TITLE_BAR_H - 2;
    fill_rect((uint32_t)(win->x + 1), body_y, win->w - 2, body_h,
              win->body_color);
}
