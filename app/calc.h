#ifndef APP_CALC_H
#define APP_CALC_H

#include <stdint.h>
#include "../wm/window.h"

/* Initialize calculator window + buttons, register with WM, draw everything. */
void calc_init(void);

/* Handle a mouse click. Returns 1 if consumed by a calculator button. */
int  calc_handle_click(int32_t x, int32_t y, uint8_t buttons);

/* Redraw calculator custom content — registered as window_t.on_redraw callback.
 * Called automatically by wm_draw_all() after the window frame is painted. */
void calc_redraw(struct window *win);

#endif
