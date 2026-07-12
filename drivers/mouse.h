#ifndef DRIVERS_MOUSE_H
#define DRIVERS_MOUSE_H

#include <stdint.h>

/* Call once during kernel init (after framebuffer is mapped). */
void mouse_init(void);

/* Called by irq_handler on vector 44 (IRQ12). */
void mouse_handle_interrupt(void);

/* Drain any stale bytes from the PS/2 output buffer.
 * Call right before sti to catch bytes the mouse sent after init. */
void mouse_drain_buf(void);

/* ── Mouse click callback (Phase 13b) ──
 * Registered by the upper layer (wm / kernel main).  Called from
 * mouse_process_packet() in IRQ12 context whenever a button state
 * change is detected.  The mouse driver knows nothing about windows
 * — it just reports (x, y, buttons) and the callback decides what
 * to do with the event. */
typedef void (*mouse_click_callback_t)(int32_t x, int32_t y, uint8_t buttons);
extern mouse_click_callback_t g_mouse_click_callback;

#endif
