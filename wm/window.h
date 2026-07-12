#ifndef WM_WINDOW_H
#define WM_WINDOW_H

#include <stdint.h>

#define TITLE_BAR_H 20

typedef struct {
    int32_t  x, y;
    uint32_t w, h;
    const char *title;
    uint32_t  title_bar_color;
    uint32_t  body_color;
} window_t;

void window_draw(const window_t *win);

#endif
