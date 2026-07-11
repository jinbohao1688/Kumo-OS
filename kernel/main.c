#include "../drivers/serial.h"
#include "../arch/x86/gdt.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/pic.h"
#include "../arch/x86/irq.h"
#include "../mm/multiboot.h"
#include "../mm/pmm.h"
#include "../arch/x86/paging.h"
#include "../mm/kheap.h"

/* ── Initialization order (HARD dependency — do not reorder) ──
 *
 *   multiboot_parse()   → fills g_memory_map
 *   pmm_init()          → bitmap allocator (reads g_memory_map)
 *   paging_init()       → allocates PD + PTs from PMM, enables paging
 *   kheap_init()        → heap (allocates pages from PMM)
 *   __asm__("sti")      → only AFTER all memory subsystems are stable
 *
 * Reordering any of the first three will produce garbage page tables
 * or triple-fault on sti. */

/* ── Test helpers ── */

static void fill8(void *p, uint32_t len, uint8_t v)
{
    uint8_t *q = (uint8_t *)p;
    for (uint32_t i = 0; i < len; i++) q[i] = v;
}

static int check8(void *p, uint32_t len, uint8_t v)
{
    uint8_t *q = (uint8_t *)p;
    for (uint32_t i = 0; i < len; i++)
        if (q[i] != v) return 0;
    return 1;
}

/* ── Heap sanity tests ── */

static void heap_tests(void)
{
    serial_write_string("=== Heap tests ===\n");

    /* ── Test 1: Basic allocation ── */
    serial_write_string("[1] kmalloc basics... ");
    uint8_t *p1 = (uint8_t *)kmalloc(32);
    uint8_t *p2 = (uint8_t *)kmalloc(64);
    uint8_t *p3 = (uint8_t *)kmalloc(128);

    if (!p1 || !p2 || !p3) {
        serial_write_string("FAIL (NULL ptr)\n");
        return;
    }
    if (p1 == p2 || p2 == p3 || p1 == p3) {
        serial_write_string("FAIL (duplicate addr)\n");
        return;
    }
    serial_write_string("OK  p1="); serial_write_hex((uint32_t)p1);
    serial_write_string(" p2=");   serial_write_hex((uint32_t)p2);
    serial_write_string(" p3=");   serial_write_hex((uint32_t)p3);
    serial_write_string("\n");

    /* ── Test 2: Write + verify ── */
    serial_write_string("[2] write+verify... ");
    fill8(p1, 32, 0xAA);
    fill8(p2, 64, 0xBB);
    fill8(p3, 128, 0xCC);

    if (!check8(p1, 32, 0xAA) || !check8(p2, 64, 0xBB) || !check8(p3, 128, 0xCC)) {
        serial_write_string("FAIL (content mismatch)\n");
        return;
    }
    serial_write_string("OK\n");

    /* ── Test 3: Free + realloc ── */
    serial_write_string("[3] free p2, realloc same size... ");
    kfree(p2);
    uint8_t *p2b = (uint8_t *)kmalloc(64);
    if (!p2b) {
        serial_write_string("FAIL (NULL on realloc)\n");
        return;
    }
    /* Verify p1 and p3 untouched */
    if (!check8(p1, 32, 0xAA) || !check8(p3, 128, 0xCC)) {
        serial_write_string("FAIL (adjacent block corrupted)\n");
        return;
    }
    serial_write_string("OK  p2b="); serial_write_hex((uint32_t)p2b);
    serial_write_string("\n");

    /* ── Test 4: Forward coalescing ──
     * Allocate 4 consecutive blocks, free A and B.  When freeing B,
     * forward coalescing checks if C is free → no.  When freeing A,
     * forward coalescing checks if B (now free) is adjacent → merge A+B. */
    serial_write_string("[4] forward coalescing... ");
    uint8_t *fa = (uint8_t *)kmalloc(48);
    uint8_t *fb = (uint8_t *)kmalloc(48);
    uint8_t *fc = (uint8_t *)kmalloc(48);

    fill8(fa, 48, 0x11);
    fill8(fb, 48, 0x22);
    fill8(fc, 48, 0x33);

    kfree(fa);   /* fa in free list */
    kfree(fb);   /* fb should merge forward... wait, forward from fb → fc is used → no merge.
                    Backward from fb → fa is free → backward merge (fa absorbs fb). */

    /* After fa absorbed fb, the combined block is in the free list.
     * Allocate a ~96B block — should come from the merged block. */
    uint8_t *merged = (uint8_t *)kmalloc(80);   /* 80 + 8 = 88, fits in 96 */
    if (!merged) {
        serial_write_string("FAIL (NULL)\n");
        return;
    }
    uint32_t m_addr = (uint32_t)merged;
    uint32_t fa_addr = (uint32_t)fa;
    if (m_addr < fa_addr || m_addr >= fa_addr + 96) {
        serial_write_string("FAIL (not from merged block)\n");
        return;
    }
    if (!check8(fc, 48, 0x33)) {
        serial_write_string("FAIL (fc corrupted)\n");
        return;
    }
    kfree(merged);
    kfree(fc);
    serial_write_string("OK\n");

    /* ── Test 5: Backward coalescing ──
     * Allocate a, b, c consecutively.  Free b, then free c.
     * When freeing c, backward walk finds b (free) → b absorbs c. */
    serial_write_string("[5] backward coalescing... ");
    uint8_t *ba = (uint8_t *)kmalloc(48); (void)ba;
    uint8_t *bb = (uint8_t *)kmalloc(48);
    uint8_t *bc = (uint8_t *)kmalloc(48);

    fill8(bb, 48, 0x55);
    fill8(bc, 48, 0x66);

    kfree(bb);   /* bb into free list */
    kfree(bc);   /* backward: find bb free → bb absorbs bc */

    /* Now allocate a ~96B block — should reuse bb+bc merged space */
    uint8_t *bmerged = (uint8_t *)kmalloc(80);
    if (!bmerged) {
        serial_write_string("FAIL (NULL)\n");
        return;
    }
    /* Write to the whole merged area to make sure it's valid */
    fill8(bmerged, 80, 0x77);
    if (!check8(bmerged, 80, 0x77)) {
        serial_write_string("FAIL (merged area broken)\n");
        return;
    }
    kfree(bmerged);
    kfree(ba);
    serial_write_string("OK\n");

    /* ── Test 6: Heap expansion ──
     * 200 × 500 bytes ≈ 100 KB > 64 KB initial heap → forces expansion. */
    serial_write_string("[6] heap expansion... ");
    uint8_t *blocks[200];
    uint32_t alloc_count = 0;

    for (int i = 0; i < 200; i++) {
        blocks[i] = (uint8_t *)kmalloc(500);
        if (!blocks[i]) {
            serial_write_string("FAIL at block ");
            serial_write_hex(i);
            serial_write_string("\n");
            return;
        }
        fill8(blocks[i], 500, (uint8_t)(i & 0xFF));
        alloc_count++;
    }

    /* Verify the first and last blocks are intact */
    if (!check8(blocks[0], 500, 0x00)) {
        serial_write_string("FAIL (first block corrupted)\n");
        return;
    }
    if (!check8(blocks[alloc_count - 1], 500, (uint8_t)((alloc_count - 1) & 0xFF))) {
        serial_write_string("FAIL (last block corrupted)\n");
        return;
    }

    /* Free them all — stresses coalescing */
    for (uint32_t i = 0; i < alloc_count; i++)
        kfree(blocks[i]);

    serial_write_string("OK (");
    serial_write_hex(alloc_count);
    serial_write_string(" blocks allocated+freed)\n");

    serial_write_string("=== All heap tests passed ===\n");
}

void kmain(unsigned int magic, void *multiboot_info) {
    serial_init();
    serial_write_string("Kumo OS booted.\n");

    gdt_init();
    idt_init();
    pic_remap();
    irq_init();

    /* ── Phase 3: memory management ── */
    multiboot_parse(magic, multiboot_info);   /* step 1/4 */

    pmm_init();                               /* step 2/4 */

    uint32_t test_page = pmm_alloc_page();
    serial_write_string("PMM sanity: pmm_alloc_page() = ");
    serial_write_hex(test_page);
    serial_write_string("\n");

    paging_init();                            /* step 3/4: identity-map + CR0.PG */

    kheap_init();                             /* step 4/4: kernel heap */
    heap_tests();

    /* ── Interrupts ── */
    serial_write_string("Enabling interrupts (sti)...\n");
    __asm__ volatile("sti");

    for (;;) {
        __asm__ volatile("hlt");
    }
}
