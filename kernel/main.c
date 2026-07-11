#include "../drivers/serial.h"
#include "../arch/x86/gdt.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/pic.h"
#include "../arch/x86/irq.h"

void kmain(unsigned int magic, void *multiboot_info) {
    (void)magic;
    (void)multiboot_info;

    serial_init();
    serial_write_string("Kumo OS booted.\n");

    gdt_init();
    idt_init();
    pic_remap();
    irq_init();

    serial_write_string("Enabling interrupts (sti)...\n");
    __asm__ volatile("sti");

    /* Idle loop: hlt wakes on any interrupt, handles it, then loops back.
       Timer IRQ0 fires ~18.2 times/sec → kernel prints tick continuously. */
    for (;;) {
        __asm__ volatile("hlt");
    }
}
