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

#endif
