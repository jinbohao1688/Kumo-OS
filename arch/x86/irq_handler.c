#include "irq.h"
#include "isr.h"
#include "idt.h"
#include "pic.h"
#include "../../drivers/serial.h"
#include "../../drivers/mouse.h"
#include "../../sched/task.h"

/* I/O port helpers */
static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0

/* IRQ entry points defined in irq.asm */
extern void irq0_entry(void);
extern void irq12_entry(void);

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

    if (r->int_no == 44) {
        mouse_handle_interrupt();
    }

    /* EOI: release the PIC so the next IRQ can be latched.
     * Sent AFTER business logic so the PIC state is consistent. */
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
    /* Wire IRQ0 (timer) → IDT vector 32. */
    idt_set_gate(32, (uint32_t)&irq0_entry, 0x08, 0x8E);

    /* Clear any stuck ISR bits by sending non-specific EOI to both PICs.
       QEMU sometimes leaves IRQ1 (keyboard) ISR set, which blocks all
       lower-priority interrupts including IRQ2 (slave cascade). */
    outb(0x20, 0x20);   /* non-specific EOI to master — clears highest ISR */
    outb(0xA0, 0x20);   /* non-specific EOI to slave */

    /* Mask IRQ1 (keyboard) since we have no driver for it, so it can
       never fire and set ISR again.  Do NOT mask IRQ2 (slave cascade)
       or IRQ0 (timer). */
    uint8_t m1 = inb(0x21);
    m1 |= (1 << 1);     /* mask IRQ1 */
    outb(0x21, m1);

    serial_write_string("IRQ0 timer handler wired (vector 32), IRQ1 masked.\n");
}
