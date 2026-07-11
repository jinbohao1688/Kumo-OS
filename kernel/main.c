#include "../drivers/serial.h"
#include "../arch/x86/gdt.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/pic.h"
#include "../arch/x86/irq.h"
#include "../mm/multiboot.h"
#include "../mm/pmm.h"
#include "../arch/x86/paging.h"

/* ── Initialization order (HARD dependency — do not reorder) ──
 *
 *   multiboot_parse()   → fills g_memory_map
 *   pmm_init()          → bitmap allocator (reads g_memory_map)
 *   paging_init()       → allocates PD + PTs from PMM, enables paging
 *   __asm__("sti")      → only AFTER paging is stable
 *
 * Reordering any of the first three will produce garbage page tables
 * or triple-fault on sti. */

void kmain(unsigned int magic, void *multiboot_info) {
    serial_init();
    serial_write_string("Kumo OS booted.\n");

    gdt_init();
    idt_init();
    pic_remap();
    irq_init();

    /* ── Phase 3: memory management ── */
    multiboot_parse(magic, multiboot_info);   /* step 1/3: extract memory map */

    pmm_init();                               /* step 2/3: page frame allocator */

    /* Sanity test: first allocation must land at or above first_free */
    uint32_t test_page = pmm_alloc_page();
    serial_write_string("PMM sanity: pmm_alloc_page() = ");
    serial_write_hex(test_page);
    serial_write_string("\n");

    paging_init();                            /* step 3/3: identity-map + CR0.PG */

    /* ── Interrupts ── */
    serial_write_string("Enabling interrupts (sti)...\n");
    __asm__ volatile("sti");

    /* Idle loop: hlt wakes on any interrupt, handles it, then loops back.
       Timer IRQ0 fires ~18.2 times/sec → kernel prints tick continuously. */
    for (;;) {
        __asm__ volatile("hlt");
    }
}
