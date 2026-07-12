#ifndef WM_WINDOW_H
#define WM_WINDOW_H

#include <stdint.h>

#define TITLE_BAR_H 20

typedef struct window {
    int32_t  x, y;
    uint32_t w, h;
    const char *title;
    uint32_t  title_bar_color;
    uint32_t  body_color;
    /* Optional: called by wm_draw_all() after the window frame is drawn,
     * for the window to paint its own custom content (buttons, display, etc.).
     * NULL means the window has no custom content beyond the frame. */
    void (*on_redraw)(struct window *self);
} window_t;

void window_draw(const window_t *win);

static inline int window_hit_test(const window_t *win, int32_t x, int32_t y)
{
    return (x >= win->x && x < (int32_t)(win->x + win->w) &&
            y >= win->y && y < (int32_t)(win->y + win->h));
}

static inline int window_hit_test_title_bar(const window_t *win, int32_t x, int32_t y)
{
    return (x >= win->x && x < (int32_t)(win->x + win->w) &&
            y >= win->y && y < (int32_t)(win->y + TITLE_BAR_H));
}

#endif
