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

    draw_string((uint32_t)(win->x + 4),
                (uint32_t)(win->y + 1 + (TITLE_BAR_H - FONT_HEIGHT) / 2),
                win->title, title_text_color);

    /* Phase 17: close button (18×18 red square + white "X", top-right) */
    uint32_t close_x = (uint32_t)(win->x + win->w - CLOSE_BTN_SIZE - CLOSE_BTN_MARGIN);
    uint32_t close_y = (uint32_t)(win->y + CLOSE_BTN_MARGIN);
    uint32_t close_color = make_color(0xCC, 0x44, 0x44);
    fill_rect(close_x, close_y, (uint32_t)CLOSE_BTN_SIZE, (uint32_t)CLOSE_BTN_SIZE,
              close_color);
    draw_string(close_x + (CLOSE_BTN_SIZE - 8) / 2,
                close_y + (CLOSE_BTN_SIZE - FONT_HEIGHT) / 2,
                "X", title_text_color);

    uint32_t body_y = (uint32_t)(win->y + TITLE_BAR_H + 1);
    uint32_t body_h = win->h - TITLE_BAR_H - 2;
    fill_rect((uint32_t)(win->x + 1), body_y, win->w - 2, body_h,
              win->body_color);
}
