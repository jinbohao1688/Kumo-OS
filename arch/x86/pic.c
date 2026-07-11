/* 8259 PIC driver — remap IRQ0-15 to vectors 32-47, plus manual EOI.

   Default mapping (conflicts with CPU exceptions 0-31):
     Master IRQ0-7:  vectors 0x08-0x0F
     Slave  IRQ8-15: vectors 0x70-0x77

   After remap:
     Master IRQ0-7:  vectors 0x20-0x27 (32-39)
     Slave  IRQ8-15: vectors 0x28-0x2F (40-47)                        */

#include "pic.h"
#include "../../drivers/serial.h"

/* ── 8259 I/O ports ── */
#define PIC1_CMD   0x20   /* master:  command */
#define PIC1_DATA  0x21   /* master:  data / IMR */
#define PIC2_CMD   0xA0   /* slave:   command */
#define PIC2_DATA  0xA1   /* slave:   data / IMR */

/* ── ICW (Initialisation Command Word) values ── */

/* ICW1: init | edge-triggered | cascade | ICW4 needed */
#define ICW1_INIT  0x11

/* ICW4: 8086/8088 mode | normal EOI (not auto) | non-buffered | normal nested */
#define ICW4_8086  0x01

/* ── I/O helpers ── */

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

/* Write to POST diagnostic port (0x80) to give the PIC time to process
   the previous command. Necessary on real hardware; harmless on QEMU. */

static void io_wait(void)
{
    outb(0x80, 0);
}

/* ── Public interface ── */

void pic_remap(void)
{
    /* Save current IMR masks so we can restore them after remap.
       All IRQs stay masked until we explicitly enable them with
       real handlers (IRQ handler phase). */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* ICW1: begin initialisation sequence */
    outb(PIC1_CMD, ICW1_INIT);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT);
    io_wait();

    /* ICW2: vector offset — IRQ0 starts at 32, IRQ8 at 40 */
    outb(PIC1_DATA, 0x20);   /* master: vector 32 */
    io_wait();
    outb(PIC2_DATA, 0x28);   /* slave:  vector 40 */
    io_wait();

    /* ICW3: cascade wiring */
    outb(PIC1_DATA, 0x04);   /* master: IRQ2 → slave */
    io_wait();
    outb(PIC2_DATA, 0x02);   /* slave:  identity = 2 (connected to master IRQ2) */
    io_wait();

    /* ICW4: 8086 mode, normal EOI */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Restore masks — all IRQs remain disabled for now.
       No sti yet, so interrupts won't fire regardless. */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);

    serial_write_string("PIC remapped (IRQ0-15 → vectors 32-47).\n");
}

void pic_send_eoi(uint8_t irq)
{
    /* IRQs from the slave PIC (8-15) need EOI sent to both PICs
       because the slave is cascaded through the master's IRQ2. */
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}
