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
#include "../fs/vfs.h"
#include "../fs/ramfs.h"
#include "../build/test_ramfs.h"
#include "../build/shell.h"
#include "../build/test_bad_ptr.h"
#include "../build/test_boundary.h"
#include "../build/test_null.h"

/* ── Initialization order (HARD dependency — do not reorder) ── */

/* ── External asm symbols ── */
extern void syscall_handler(void);

/* ── User-mode test code — loaded from user/test_ramfs.asm via build/test_ramfs.h ──
 *
 * The binary (build_test_ramfs_bin) is position-independent; it uses
 * call/pop to discover its runtime address.  Data (path, write buffer,
 * read buffer) is embedded at the end of the binary.
 *
 * Logic:
 *   open("/test", WRONLY) → write("hello", 5) → close
 *   open("/test", RDONLY) → read(rbuf, 5) → close
 *   PRINT each char of rbuf → should output "hello"
 *   Loop with SYSCALL_YIELD */

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

/* ── Phase 7: Run registry ── */

#define RUN_MAX_ENTRIES 8

typedef struct {
    char name[32];
    uint32_t code_page;
} run_entry_t;

static run_entry_t g_run_registry[RUN_MAX_ENTRIES];
static int g_run_count = 0;

static void run_register(const char *name, uint32_t code_page)
{
    if (g_run_count >= RUN_MAX_ENTRIES) return;
    int i;
    for (i = 0; name[i] && i < 31; i++)
        g_run_registry[g_run_count].name[i] = name[i];
    g_run_registry[g_run_count].name[i] = 0;
    g_run_registry[g_run_count].code_page = code_page;
    g_run_count++;

    serial_write_string("Run: registered '");
    serial_write_string((char *)name);
    serial_write_string("' at ");
    serial_write_hex(code_page);
    serial_write_string("\n");
}

int run_exec(const char *name)
{
    for (int i = 0; i < g_run_count; i++) {
        int match = 1;
        for (int j = 0; j < 32; j++) {
            if (g_run_registry[i].name[j] != name[j]) { match = 0; break; }
            if (name[j] == 0) break;
        }
        if (match) {
            task_t *t = task_create_user(g_run_registry[i].code_page, name[0]);
            if (t) {
                serial_write_string("Run: launched '");
                serial_write_string((char *)name);
                serial_write_string("' as task ");
                serial_write_hex(t->id);
                serial_write_string("\n");
                return (int)t->id;
            }
            return -1;
        }
    }
    return -1;
}

/* ── Phase 7: Shell + embedded tests ── */

static void copy_code(uint32_t dest_page, const uint8_t *src, uint32_t len)
{
    uint8_t *d = (uint8_t *)dest_page;
    for (uint32_t i = 0; i < len; i++)
        d[i] = src[i];
}

void kmain(unsigned int magic, void *multiboot_info) {
    serial_init();
    serial_write_string("Kumo OS booted.\n");

    gdt_init();
    idt_init();
    pic_remap();
    irq_init();

    /* ── Memory management ── */
    multiboot_parse(magic, multiboot_info);

    pmm_init();

    uint32_t test_page = pmm_alloc_page();
    serial_write_string("PMM sanity: pmm_alloc_page() = ");
    serial_write_hex(test_page);
    serial_write_string("\n");

    paging_init();

    /* ADR-003: unmap NULL page before any user code runs */
    paging_unmap_null_page();

    kheap_init();
    heap_tests();

    /* ── Phase 5: TSS + syscall gate ── */
    tss_init();
    idt_set_gate(0x80, (uint32_t)&syscall_handler, KERNEL_CS, 0xEE);
    serial_write_string("Syscall: IDT[0x80] wired (DPL=3)\n");

    /* ── VFS + RamFS ── */
    vfs_init();
    vfs_node_t *ramfs_root = ramfs_init();
    if (ramfs_root) {
        vfs_mount("/", ramfs_root);
    } else {
        serial_write_string("ERROR: ramfs_init failed!\n");
        for (;;) { __asm__ volatile("hlt"); }
    }

    /* ── Interrupts ── */
    serial_write_string("Enabling interrupts (sti)...\n");
    __asm__ volatile("sti");

    /* ── Phase 7: Shell + test program registry ── */
    serial_write_string("\n=== Phase 7: Shell ===\n");

    /* Register embedded test programs (before creating shell) */
    uint32_t ramfs_test_page = pmm_alloc_page();
    paging_set_user_accessible(ramfs_test_page);
    copy_code(ramfs_test_page, build_test_ramfs_bin, build_test_ramfs_bin_len);
    run_register("ramfs_test", ramfs_test_page);

    /* ADR-004: bad-pointer test */
    uint32_t bad_ptr_page = pmm_alloc_page();
    paging_set_user_accessible(bad_ptr_page);
    copy_code(bad_ptr_page, build_test_bad_ptr_bin, build_test_bad_ptr_bin_len);
    run_register("bad_ptr", bad_ptr_page);

    /* ADR-004: page-boundary string test */
    uint32_t boundary_page = pmm_alloc_page();
    paging_set_user_accessible(boundary_page);
    copy_code(boundary_page, build_test_boundary_bin, build_test_boundary_bin_len);
    run_register("boundary", boundary_page);

    /* ADR-003: NULL deref test (runs last — triggers #PF + halt) */
    uint32_t null_page = pmm_alloc_page();
    paging_set_user_accessible(null_page);
    copy_code(null_page, build_test_null_bin, build_test_null_bin_len);
    run_register("null_test", null_page);

    /* Shell user task */
    uint32_t shell_page = pmm_alloc_page();
    paging_set_user_accessible(shell_page);
    copy_code(shell_page, build_shell_bin, build_shell_bin_len);

    serial_write_string("Shell: code page = ");
    serial_write_hex(shell_page);
    serial_write_string(" (");
    serial_write_hex(build_shell_bin_len);
    serial_write_string(" bytes)\n");

    task_init();
    task_create_user(shell_page, 'S');   /* 'S' = Shell */

    serial_write_string("Shell: entering idle/scheduler loop...\n");
    for (;;) {
        task_yield();
    }
}
