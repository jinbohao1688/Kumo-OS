#include "../drivers/serial.h"
#include "../arch/x86/gdt.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/pic.h"
#include "../arch/x86/irq.h"
#include "../mm/multiboot.h"
#include "../mm/pmm.h"
#include "../arch/x86/paging.h"
#include "../mm/kheap.h"
#include "../sched/task.h"
#include "../arch/x86/tss.h"
#include "../arch/x86/syscall.h"

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

/* ── External asm symbols ── */
extern void enter_ring3(uint32_t entry, uint32_t user_stack_top);
extern void syscall_handler(void);

/* ── User-mode test code (binary blob, copied to user code page) ──
 *
 * Disassembly:
 *   loop:
 *     mov eax, 1          ; B8 01 00 00 00  — syscall number = 1 (print)
 *     mov ebx, 0xBEEF     ; BB EF BE 00 00  — arg1 = 0xBEEF
 *     int 0x80             ; CD 80           — trigger syscall
 *     jmp loop            ; EB F2           — jmp $-14 (back to offset 0)
 *
 * Total: 5 + 5 + 2 + 2 = 14 bytes */

static const uint8_t user_test_code[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,   /* mov $1, %eax */
    0xBB, 0xEF, 0xBE, 0x00, 0x00,   /* mov $0xBEEF, %ebx */
    0xCD, 0x80,                       /* int $0x80 */
    0xEB, 0xF2,                       /* jmp back to offset 0 */
};

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
    if (!check8(p1, 32, 0xAA) || !check8(p3, 128, 0xCC)) {
        serial_write_string("FAIL (adjacent block corrupted)\n");
        return;
    }
    serial_write_string("OK  p2b="); serial_write_hex((uint32_t)p2b);
    serial_write_string("\n");

    /* ── Test 4: Forward coalescing ── */
    serial_write_string("[4] forward coalescing... ");
    uint8_t *fa = (uint8_t *)kmalloc(48);
    uint8_t *fb = (uint8_t *)kmalloc(48);
    uint8_t *fc = (uint8_t *)kmalloc(48);

    fill8(fa, 48, 0x11);
    fill8(fb, 48, 0x22);
    fill8(fc, 48, 0x33);

    kfree(fa);
    kfree(fb);

    uint8_t *merged = (uint8_t *)kmalloc(80);
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

    /* ── Test 5: Backward coalescing ── */
    serial_write_string("[5] backward coalescing... ");
    uint8_t *ba = (uint8_t *)kmalloc(48); (void)ba;
    uint8_t *bb = (uint8_t *)kmalloc(48);
    uint8_t *bc = (uint8_t *)kmalloc(48);

    fill8(bb, 48, 0x55);
    fill8(bc, 48, 0x66);

    kfree(bb);
    kfree(bc);

    uint8_t *bmerged = (uint8_t *)kmalloc(80);
    if (!bmerged) {
        serial_write_string("FAIL (NULL)\n");
        return;
    }
    fill8(bmerged, 80, 0x77);
    if (!check8(bmerged, 80, 0x77)) {
        serial_write_string("FAIL (merged area broken)\n");
        return;
    }
    kfree(bmerged);
    kfree(ba);
    serial_write_string("OK\n");

    /* ── Test 6: Heap expansion ── */
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

    if (!check8(blocks[0], 500, 0x00)) {
        serial_write_string("FAIL (first block corrupted)\n");
        return;
    }
    if (!check8(blocks[alloc_count - 1], 500, (uint8_t)((alloc_count - 1) & 0xFF))) {
        serial_write_string("FAIL (last block corrupted)\n");
        return;
    }

    for (uint32_t i = 0; i < alloc_count; i++)
        kfree(blocks[i]);

    serial_write_string("OK (");
    serial_write_hex(alloc_count);
    serial_write_string(" blocks allocated+freed)\n");

    serial_write_string("=== All heap tests passed ===\n");
}

/* ── Phase 5: Ring3 test ── */

static void ring3_test(void)
{
    serial_write_string("\n=== Phase 5: Ring0 → Ring3 transition ===\n");

    /* 1. Allocate user code page (1 page = 4096 bytes) */
    uint32_t user_code_page = pmm_alloc_page();
    if (!user_code_page) {
        serial_write_string("Ring3: ERROR — cannot alloc user code page\n");
        return;
    }
    serial_write_string("Ring3: user code page = ");
    serial_write_hex(user_code_page);
    serial_write_string("\n");

    /* 2. Allocate user stack page */
    uint32_t user_stack_page = pmm_alloc_page();
    if (!user_stack_page) {
        serial_write_string("Ring3: ERROR — cannot alloc user stack page\n");
        return;
    }
    serial_write_string("Ring3: user stack page = ");
    serial_write_hex(user_stack_page);
    serial_write_string("\n");

    /* 3. Allocate kernel stack page for syscall handler (TSS.esp0) */
    uint32_t kstack_page = pmm_alloc_page();
    if (!kstack_page) {
        serial_write_string("Ring3: ERROR — cannot alloc kernel stack page\n");
        return;
    }
    uint32_t kstack_top = kstack_page + PAGE_SIZE;
    serial_write_string("Ring3: kernel stack (TSS.esp0) page = ");
    serial_write_hex(kstack_page);
    serial_write_string(" top = ");
    serial_write_hex(kstack_top);
    serial_write_string("\n");

    /* 4. Mark user pages as user-accessible (set U/S=1 in PTEs) */
    paging_set_user_accessible(user_code_page);
    paging_set_user_accessible(user_stack_page);

    /* 5. Copy user test code to the code page */
    uint8_t *code_dest = (uint8_t *)user_code_page;
    for (uint32_t i = 0; i < sizeof(user_test_code); i++)
        code_dest[i] = user_test_code[i];

    serial_write_string("Ring3: copied ");
    serial_write_hex(sizeof(user_test_code));
    serial_write_string(" bytes of user code to ");
    serial_write_hex(user_code_page);
    serial_write_string("\n");

    /* 6. Set TSS.esp0 = kernel stack top (for syscall handler) */
    tss_set_esp0(kstack_top);
    serial_write_string("Ring3: TSS.esp0 = ");
    serial_write_hex(kstack_top);
    serial_write_string("\n");

    /* 7. Wire IDT[0x80] — syscall handler, DPL=3 */
    extern void idt_set_gate(int vector, uint32_t handler_addr,
                             uint16_t selector, uint8_t type_attr);
    idt_set_gate(0x80, (uint32_t)&syscall_handler, KERNEL_CS, 0xEE);
    /*                                    type_attr=0xEE:  P=1 DPL=3 GateType=0xE (32-bit intr gate) */
    serial_write_string("Ring3: IDT[0x80] wired (DPL=3, intr gate)\n");

    /* 8. Enter Ring3 — this call never returns */
    serial_write_string("Ring3: jumping to user mode...\n");
    uint32_t user_stack_top = user_stack_page + PAGE_SIZE;
    enter_ring3(user_code_page, user_stack_top);

    /* NOTREACHED — execution continues in Ring3 via user_test_code */
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

    /* ── Phase 5: TSS + Ring0→Ring3 + syscall ──
     * (temporarily replaces Phase 4 idle loop; re-integration in Phase 6) */
    tss_init();
    ring3_test();

    /* NOTREACHED — ring3_test calls enter_ring3() which never returns.
     * If we do get here (e.g. syscall iret fails), halt. */
    serial_write_string("ERROR: returned from ring3_test unexpectedly!\n");
    for (;;) { __asm__ volatile("hlt"); }
}
