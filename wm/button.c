#include "button.h"
#include "../gfx/primitives.h"
#include "../gfx/font.h"
#include "../drivers/serial.h"
#include <stddef.h>

static button_t        *g_pressed_button;
static const window_t  *g_pressed_parent;

void button_draw(const button_t *btn, const window_t *parent, int pressed)
{
    int32_t  abs_x = parent->x + btn->x;
    int32_t  abs_y = parent->y + btn->y;
    uint32_t color = pressed ? btn->color_pressed : btn->color_idle;

    fill_rect((uint32_t)abs_x, (uint32_t)abs_y,
              btn->w - 1, btn->h - 1, color);

    uint32_t border = make_color(0x30, 0x30, 0x30);
    draw_rect((uint32_t)abs_x, (uint32_t)abs_y,
              btn->w - 1, btn->h - 1, border);

    uint32_t text_color = make_color(0xFF, 0xFF, 0xFF);
    draw_string((uint32_t)(abs_x + 6), (uint32_t)(abs_y + 6),
                btn->label, text_color);
}

int button_handle_down(button_t *btn, const window_t *parent,
                       int32_t x, int32_t y)
{
    (void)x; (void)y;   /* already hit-tested by caller */

    g_pressed_button = btn;
    g_pressed_parent = parent;
    button_draw(btn, parent, 1);

    serial_write_string("Button: pressed '");
    serial_write_string((char *)btn->label);
    serial_write_string("'\n");

    return 1;
}

int button_handle_up(int32_t x, int32_t y)
{
    if (!g_pressed_button) return 0;

    button_t *btn    = g_pressed_button;
    const window_t *parent = g_pressed_parent;

    if (button_hit_test(btn, parent, x, y)) {
        serial_write_string("Button: clicked! '");
        serial_write_string((char *)btn->label);
        serial_write_string("'\n");

        if (btn->on_click)
            btn->on_click();
    } else {
        serial_write_string("Button: cancelled '");
        serial_write_string((char *)btn->label);
        serial_write_string("'\n");
    }

    button_draw(btn, parent, 0);
    g_pressed_button = NULL;
    g_pressed_parent = NULL;

    return 1;
}
