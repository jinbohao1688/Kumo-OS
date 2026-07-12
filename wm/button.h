#ifndef WM_BUTTON_H
#define WM_BUTTON_H

#include <stdint.h>
#include "window.h"

typedef struct {
    int32_t  x, y;           /* relative to parent window origin */
    uint32_t w, h;
    const char *label;
    uint32_t color_idle;
    uint32_t color_pressed;
    void   (*on_click)(void);
} button_t;

/* Hit test: (x,y) in absolute screen coords */
static inline int button_hit_test(const button_t *btn, const window_t *parent,
                                  int32_t x, int32_t y)
{
    int32_t abs_x = parent->x + btn->x;
    int32_t abs_y = parent->y + btn->y;
    return (x >= abs_x && x < abs_x + (int32_t)btn->w &&
            y >= abs_y && y < abs_y + (int32_t)btn->h);
}

void button_draw(const button_t *btn, const window_t *parent, int pressed);

/* Called on mouse-down.  Returns 1 if button consumed the event. */
int  button_handle_down(button_t *btn, const window_t *parent,
                        int32_t x, int32_t y);

/* Called on mouse-up.  Returns 1 if a pressed button consumed the event. */
int  button_handle_up(int32_t x, int32_t y);

#endif
