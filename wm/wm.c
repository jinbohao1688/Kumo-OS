#include "wm.h"
#include "../gfx/primitives.h"
#include "../mm/multiboot.h"
#include "../drivers/mouse.h"
#include "../drivers/serial.h"

static window_t *g_windows[MAX_WINDOWS];
static int       g_window_count;

void wm_add_window(window_t *win)
{
    if (g_window_count >= MAX_WINDOWS) return;
    g_windows[g_window_count++] = win;
}

void wm_draw_all(void)
{
    mouse_cursor_hide();

    uint32_t desktop = make_color(0x20, 0x20, 0x30);
    fill_rect(0, 0, g_framebuffer.width - 1, g_framebuffer.height - 1, desktop);

    for (int i = 0; i < g_window_count; i++) {
        window_draw(g_windows[i]);
        if (g_windows[i]->on_redraw)
            g_windows[i]->on_redraw(g_windows[i]);
    }

    mouse_cursor_show();
}

void wm_handle_click(int32_t x, int32_t y, uint8_t buttons)
{
    (void)buttons;

    /* Hit test top-to-bottom */
    for (int i = g_window_count - 1; i >= 0; i--) {
        if (window_hit_test(g_windows[i], x, y)) {
            serial_write_string("WM: click hit '");
            serial_write_string((char *)g_windows[i]->title);
            serial_write_string("'");

            if (i == g_window_count - 1) {
                serial_write_string(" (already top)\n");
                return;
            }

            /* Bring to top: shift windows above down, place at end */
            window_t *hit = g_windows[i];
            for (int j = i; j < g_window_count - 1; j++)
                g_windows[j] = g_windows[j + 1];
            g_windows[g_window_count - 1] = hit;

            serial_write_string(" -> Z(bottom->top): ");
            for (int k = 0; k < g_window_count; k++) {
                if (k > 0) serial_write_string(", ");
                serial_write_string((char *)g_windows[k]->title);
            }
            serial_write_string("\n");

            wm_draw_all();
            return;
        }
    }

    serial_write_string("WM: click (");
    serial_write_hex((uint32_t)x);
    serial_write_string(", ");
    serial_write_hex((uint32_t)y);
    serial_write_string(") -> no window hit\n");
}
