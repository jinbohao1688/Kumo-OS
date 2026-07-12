#include "calc.h"
#include "../wm/wm.h"
#include "../wm/button.h"
#include "../wm/window.h"
#include "../gfx/primitives.h"
#include "../gfx/font.h"
#include "../drivers/serial.h"
#include <stddef.h>

#define MAX_BTNS 20

/* ── Calculator state ── */
static int32_t  g_accumulator;
static int32_t  g_current;
static uint8_t  g_pending_op;  /* 0=none, 1=+, 2=-, 3=*, 4=/ */
static int      g_entering;

/* ── Window + button array ── */
static window_t  g_calc_win;
static button_t  g_btns[MAX_BTNS];
static int       g_btn_count;

/* ── Display area (relative to window) ── */
#define DISP_X   10
#define DISP_Y   25
#define DISP_W   220
#define DISP_H   40
#define DISP_RPAD 8

/* ── Number → string ── */
static void itoa(int32_t n, char *buf)
{
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    if (n < 0) { *buf = '-'; n = -n; buf++; }
    char tmp[16];
    int i = 0;
    while (n > 0) { tmp[i++] = '0' + (char)(n % 10); n /= 10; }
    while (i > 0) *buf++ = tmp[--i];
    *buf = '\0';
}

/* ── Refresh display area ── */
static void calc_refresh_display(void)
{
    int32_t abs_x = g_calc_win.x + DISP_X;
    int32_t abs_y = g_calc_win.y + DISP_Y;

    int32_t value = g_entering ? g_current : g_accumulator;
    char buf[16];
    itoa(value, buf);

    /* Erase with body color */
    fill_rect((uint32_t)abs_x, (uint32_t)abs_y,
              DISP_W, DISP_H, g_calc_win.body_color);

    /* Right-align: compute string pixel width */
    int slen = 0;
    while (buf[slen]) slen++;
    int32_t text_x = abs_x + (int32_t)DISP_W - (int32_t)DISP_RPAD - slen * 8;
    if (text_x < abs_x + 4) text_x = abs_x + 4;

    uint32_t text_color = make_color(0x10, 0x10, 0x10);
    draw_string((uint32_t)text_x, (uint32_t)(abs_y + 12), buf, text_color);

    serial_write_string("Calc: display \"");
    serial_write_string(buf);
    serial_write_string("\"\n");
}

/* ── State machine helpers ── */

static void calc_execute_pending(void)
{
    switch (g_pending_op) {
    case 1: g_accumulator += g_current; break; /* + */
    case 2: g_accumulator -= g_current; break; /* - */
    case 3: g_accumulator *= g_current; break; /* * */
    case 4: if (g_current != 0) g_accumulator /= g_current; break; /* / */
    default: break;
    }
}

static void calc_input_digit(int d)
{
    if (g_entering)
        g_current = g_current * 10 + d;
    else {
        g_current = d;
        g_entering = 1;
    }
    calc_refresh_display();
}

static void calc_input_op(uint8_t op)
{
    if (g_pending_op != 0) {
        calc_execute_pending();
        g_accumulator = g_accumulator;  /* result stays */
    } else {
        g_accumulator = g_current;
    }
    g_pending_op = op;
    g_current = 0;
    g_entering = 0;
    calc_refresh_display();
}

static void calc_do_equals(void)
{
    if (g_pending_op != 0) {
        calc_execute_pending();
        g_current = g_accumulator;
    }
    g_accumulator = g_current;
    g_pending_op = 0;
    g_entering = 0;
    calc_refresh_display();
}

static void calc_do_clear(void)
{
    g_current     = 0;
    g_accumulator = 0;
    g_pending_op  = 0;
    g_entering    = 0;
    calc_refresh_display();
}

/* ── Button callbacks (one per button, 16 total) ── */
static void cb_d0(void) { calc_input_digit(0); }
static void cb_d1(void) { calc_input_digit(1); }
static void cb_d2(void) { calc_input_digit(2); }
static void cb_d3(void) { calc_input_digit(3); }
static void cb_d4(void) { calc_input_digit(4); }
static void cb_d5(void) { calc_input_digit(5); }
static void cb_d6(void) { calc_input_digit(6); }
static void cb_d7(void) { calc_input_digit(7); }
static void cb_d8(void) { calc_input_digit(8); }
static void cb_d9(void) { calc_input_digit(9); }
static void cb_add(void) { calc_input_op(1); }
static void cb_sub(void) { calc_input_op(2); }
static void cb_mul(void) { calc_input_op(3); }
static void cb_div(void) { calc_input_op(4); }
static void cb_eq(void)  { calc_do_equals(); }
static void cb_clr(void) { calc_do_clear(); }

/* ── Helper: add one button to the array ── */
static void add_btn(int32_t rx, int32_t ry, const char *label,
                    uint32_t color_idle, uint32_t color_pressed, void (*cb)(void))
{
    if (g_btn_count >= MAX_BTNS) return;
    button_t *b = &g_btns[g_btn_count++];
    b->x = rx; b->y = ry; b->w = 52; b->h = 35;
    b->label = label;
    b->color_idle    = color_idle;
    b->color_pressed = color_pressed;
    b->on_click = cb;
}

/* ── Public API ── */

void calc_init(void)
{
    uint32_t c_digit     = make_color(0x90, 0x95, 0xA0);
    uint32_t c_digit_pr  = make_color(0x60, 0x65, 0x70);
    uint32_t c_op        = make_color(0xD0, 0xA0, 0x60);
    uint32_t c_op_pr     = make_color(0xC0, 0x70, 0x20);
    uint32_t c_eq        = make_color(0x60, 0xB0, 0x60);
    uint32_t c_eq_pr     = make_color(0x30, 0x90, 0x30);

    /* Window */
    g_calc_win.x = 500; g_calc_win.y = 40;
    g_calc_win.w = 240; g_calc_win.h = 280;
    g_calc_win.title = "Calculator";
    g_calc_win.title_bar_color = make_color(0x30, 0x40, 0x60);
    g_calc_win.body_color       = make_color(0xE0, 0xE0, 0xE0);
    g_calc_win.on_redraw        = calc_redraw;

    wm_add_window(&g_calc_win);

    /* Button grid: 4 rows × 4 cols, 52×35 each, 4px gap */
    int32_t x0 = 10, x1 = 66, x2 = 122, x3 = 178;
    int32_t y0 = 69, y1 = 108, y2 = 147, y3 = 186;

    add_btn(x0, y0, "7", c_digit, c_digit_pr, cb_d7);
    add_btn(x1, y0, "8", c_digit, c_digit_pr, cb_d8);
    add_btn(x2, y0, "9", c_digit, c_digit_pr, cb_d9);
    add_btn(x3, y0, "/", c_op,    c_op_pr,    cb_div);

    add_btn(x0, y1, "4", c_digit, c_digit_pr, cb_d4);
    add_btn(x1, y1, "5", c_digit, c_digit_pr, cb_d5);
    add_btn(x2, y1, "6", c_digit, c_digit_pr, cb_d6);
    add_btn(x3, y1, "*", c_op,    c_op_pr,    cb_mul);

    add_btn(x0, y2, "1", c_digit, c_digit_pr, cb_d1);
    add_btn(x1, y2, "2", c_digit, c_digit_pr, cb_d2);
    add_btn(x2, y2, "3", c_digit, c_digit_pr, cb_d3);
    add_btn(x3, y2, "-", c_op,    c_op_pr,    cb_sub);

    add_btn(x0, y3, "C", c_eq,    c_eq_pr,    cb_clr);
    add_btn(x1, y3, "0", c_digit, c_digit_pr, cb_d0);
    add_btn(x2, y3, "=", c_eq,    c_eq_pr,    cb_eq);
    add_btn(x3, y3, "+", c_op,    c_op_pr,    cb_add);

    /* Draw window + all buttons */
    window_draw(&g_calc_win);
    for (int i = 0; i < g_btn_count; i++)
        button_draw(&g_btns[i], &g_calc_win, 0);

    /* Clear display */
    calc_refresh_display();

    serial_write_string("Calc: init done, ");
    serial_write_hex((uint32_t)g_btn_count);
    serial_write_string(" buttons\n");
}

void calc_redraw(struct window *win)
{
    (void)win;  /* we know it's &g_calc_win */
    window_draw(&g_calc_win);
    for (int i = 0; i < g_btn_count; i++)
        button_draw(&g_btns[i], &g_calc_win, 0);
    calc_refresh_display();
}

int calc_handle_click(int32_t x, int32_t y, uint8_t buttons)
{
    if (buttons & 1) {
        /* Mouse-down: hit test all buttons */
        for (int i = 0; i < g_btn_count; i++) {
            if (button_hit_test(&g_btns[i], &g_calc_win, x, y)) {
                button_handle_down(&g_btns[i], &g_calc_win, x, y);
                return 1;
            }
        }
    } else {
        /* Mouse-up: if a button was pressed, handle release */
        if (button_handle_up(x, y))
            return 1;
    }
    return 0;
}
