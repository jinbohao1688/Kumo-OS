#include "irq.h"
#include "isr.h"
#include "idt.h"
#include "pic.h"
#include "../../drivers/serial.h"

/* IRQ0 entry point defined in irq.asm */
extern void irq0_entry(void);

static uint32_t tick_count = 0;

/* irq_handler — common C handler for all IRQs.
   Dispatches on vector number, then sends EOI.
   EOI is sent AFTER business logic: if print hangs,
   the PIC freezes → a clear diagnostic signal. */

void irq_handler(registers_t *r)
{
    if (r->int_no == 32) {
        tick_count++;
        serial_write_string("tick ");
        serial_write_hex(tick_count);
        serial_write_string("\n");
    }

    /* EOI last: the PIC only gets released once we're done.
       r->int_no - 32 converts vector → IRQ number
       (32 → 0, 33 → 1, …, 47 → 15). */
    pic_send_eoi((uint8_t)(r->int_no - 32));
}

void irq_init(void)
{
    /* Wire IRQ0 (timer) → IDT vector 32.
       Other IRQs (33-47) remain at isr_stub for now. */
    idt_set_gate(32, (uint32_t)&irq0_entry, 0x08, 0x8E);

    serial_write_string("IRQ0 timer handler wired (vector 32).\n");
}
