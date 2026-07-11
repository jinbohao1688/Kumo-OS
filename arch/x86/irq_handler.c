#include "irq.h"
#include "isr.h"
#include "idt.h"
#include "pic.h"
#include "../../drivers/serial.h"
#include "../../sched/task.h"

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

        /* Reduced output: every 50 ticks (~2.75s at 18.2 Hz) */
        if (tick_count % 50 == 0) {
            serial_write_string("tick ");
            serial_write_hex(tick_count);
            serial_write_string("\n");
        }
    }

    /* EOI before schedule(): release the PIC so the next timer
     * IRQ can be latched while we're in a different task.
     * IF is still 0 (interrupt gate), so it won't actually fire
     * until the next iret. */
    pic_send_eoi((uint8_t)(r->int_no - 32));

    /* Preemptive scheduling: only on timer IRQ (vector 32).
     * task_yield() does cli internally — safe even though IF is
     * already 0 from the interrupt gate. */
    if (r->int_no == 32) {
        task_yield();
    }
}

void irq_init(void)
{
    /* Wire IRQ0 (timer) → IDT vector 32.
       Other IRQs (33-47) remain at isr_stub for now. */
    idt_set_gate(32, (uint32_t)&irq0_entry, 0x08, 0x8E);

    serial_write_string("IRQ0 timer handler wired (vector 32).\n");
}
