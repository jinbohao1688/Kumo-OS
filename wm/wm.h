#ifndef WM_WM_H
#define WM_WM_H

#include <stdint.h>
#include "window.h"

#define MAX_WINDOWS 16

void wm_add_window(window_t *win);
void wm_draw_all(void);
void wm_handle_click(int32_t x, int32_t y, uint8_t buttons);

/* Phase 16: window drag support */
window_t *wm_find_window_at(int32_t x, int32_t y);
void      wm_bring_to_top(window_t *win);

/* Phase 17: close button */
void wm_remove_window(window_t *win);

#endif
