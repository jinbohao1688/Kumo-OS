#include "../drivers/serial.h"
#include "../arch/x86/gdt.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/pic.h"
#include "../arch/x86/irq.h"
#include "../mm/multiboot.h"
#include "../mm/pmm.h"

void kmain(unsigned int magic, void *multiboot_info) {
    serial_init();
    serial_write_string("Kumo OS booted.\n");

    gdt_init();
    idt_init();
    pic_remap();
    irq_init();
    multiboot_parse(magic, multiboot_info);

    pmm_init();

    /* Sanity test: first allocation must land at or above first_free */
    uint32_t test_page = pmm_alloc_page();
    serial_write_string("PMM sanity: pmm_alloc_page() = ");
    serial_write_hex(test_page);
    serial_write_string("\n");

    serial_write_string("Enabling interrupts (sti)...\n");
    __asm__ volatile("sti");

    /* Idle loop: hlt wakes on any interrupt, handles it, then loops back.
       Timer IRQ0 fires ~18.2 times/sec → kernel prints tick continuously. */
    for (;;) {
        __asm__ volatile("hlt");
    }
}
