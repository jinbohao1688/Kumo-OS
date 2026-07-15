#include <stddef.h>
#include "wm.h"
#include "../gfx/primitives.h"
#include "../mm/multiboot.h"
#include "../drivers/mouse.h"
#include "../drivers/serial.h"

static window_t *g_windows[MAX_WINDOWS];
static int       g_window_count;

/* ── Phase 18 step 1: Damage tracking state (drag only) ── */
static struct {
    int32_t  x, y;
    uint32_t w, h;
    int      valid;
} g_dirty;

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

window_t *wm_find_window_at(int32_t x, int32_t y)
{
    for (int i = g_window_count - 1; i >= 0; i--) {
        if (window_hit_test(g_windows[i], x, y))
            return g_windows[i];
    }
    return NULL;
}

void wm_bring_to_top(window_t *win)
{
    int idx = -1;
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i] == win) { idx = i; break; }
    }
    if (idx < 0 || idx == g_window_count - 1) return;

    for (int j = idx; j < g_window_count - 1; j++)
        g_windows[j] = g_windows[j + 1];
    g_windows[g_window_count - 1] = win;

    wm_draw_all();
}

void wm_remove_window(window_t *win)
{
    int idx = -1;
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i] == win) { idx = i; break; }
    }
    if (idx < 0) return;

    serial_write_string("WM: removing '");
    serial_write_string((char *)win->title);
    serial_write_string("'\n");

    for (int j = idx; j < g_window_count - 1; j++)
        g_windows[j] = g_windows[j + 1];
    g_windows[g_window_count - 1] = NULL;
    g_window_count--;

    wm_draw_all();
}

int wm_is_window_active(window_t *win)
{
    for (int i = 0; i < g_window_count; i++) {
        if (g_windows[i] == win) return 1;
    }
    return 0;
}

/* ── Phase 18 step 1: Dirty rectangle tracking ── */

void wm_mark_dirty(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) return;

    /* Clamp to framebuffer bounds */
    if (x < 0) {
        uint32_t adj = (uint32_t)(-x);
        if (adj >= w) return;
        w -= adj; x = 0;
    }
    if (y < 0) {
        uint32_t adj = (uint32_t)(-y);
        if (adj >= h) return;
        h -= adj; y = 0;
    }
    if ((uint32_t)x >= g_framebuffer.width || (uint32_t)y >= g_framebuffer.height) return;
    if ((uint32_t)x + w > g_framebuffer.width)  w = g_framebuffer.width  - (uint32_t)x;
    if ((uint32_t)y + h > g_framebuffer.height) h = g_framebuffer.height - (uint32_t)y;
    if (w == 0 || h == 0) return;

    if (!g_dirty.valid) {
        g_dirty.x = x; g_dirty.y = y;
        g_dirty.w = w; g_dirty.h = h;
        g_dirty.valid = 1;
        return;
    }

    /* Expand bounding rectangle */
    int32_t dr = g_dirty.x + (int32_t)g_dirty.w;
    int32_t db = g_dirty.y + (int32_t)g_dirty.h;
    int32_t nr = x + (int32_t)w;
    int32_t nb = y + (int32_t)h;

    if (x < g_dirty.x) g_dirty.x = x;
    if (y < g_dirty.y) g_dirty.y = y;
    if (nr > dr) dr = nr;
    if (nb > db) db = nb;

    g_dirty.w = (uint32_t)(dr - g_dirty.x);
    g_dirty.h = (uint32_t)(db - g_dirty.y);
}

int wm_has_dirty(void)
{
    return g_dirty.valid;
}

void wm_flush_dirty(void)
{
    /* Snapshot-and-clear: atomically take ownership so IRQ12 mark_dirty
     * writes to a fresh g_dirty while we work from the local snapshot. */
    struct { int32_t x, y; uint32_t w, h; int valid; } snap;
    int had_dirty;

    __asm__ volatile("cli");
    had_dirty = g_dirty.valid;
    if (had_dirty) {
        snap.x = g_dirty.x;
        snap.y = g_dirty.y;
        snap.w = g_dirty.w;
        snap.h = g_dirty.h;
        snap.valid = 1;
        g_dirty.valid = 0;
    }
    __asm__ volatile("sti");

    if (!had_dirty) return;

    /* The flush sequence runs under cli so IRQ12 cursor restore/draw
     * cannot interleave with cursor hide/show + framebuffer writes.
     *
     * cli duration = fill_rect(dirty) + sum of window_draw(intersecting).
     * Typical drag: 200K-400K px -> 20-40 ms.
     * Corner-to-corner drag: ~786K px -> ~100-150 ms.
     * If freezes reproduce on extreme drags, split fill_rect into
     * horizontal strips with per-strip cli/sti. */
    __asm__ volatile("cli");
    mouse_cursor_hide();

    uint32_t desktop = make_color(0x20, 0x20, 0x30);
    fill_rect((uint32_t)snap.x, (uint32_t)snap.y, snap.w, snap.h, desktop);

    for (int i = 0; i < g_window_count; i++) {
        window_t *w = g_windows[i];
        int32_t wr = w->x + (int32_t)w->w;
        int32_t wb = w->y + (int32_t)w->h;
        int32_t dr = snap.x + (int32_t)snap.w;
        int32_t db = snap.y + (int32_t)snap.h;

        if (!(w->x >= dr || wr <= snap.x || w->y >= db || wb <= snap.y)) {
            window_draw(w);
            if (w->on_redraw) w->on_redraw(w);
        }
    }

    mouse_cursor_show();
    __asm__ volatile("sti");
}

