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
#include "../build/hello_elf.h"
#include "../build/regtest_a.h"
#include "../build/regtest_b.h"
#include "../fs/elf.h"
#include "../gfx/primitives.h"
#include "../gfx/font.h"

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
            task_t *t = task_create_user(g_run_registry[i].code_page, name[0], 0);
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

/* ── Phase 8b: ELF exec from VFS ── */

#define ELF_LOAD_MAX  (16 * 1024)    /* max ELF file size; heap blocks are 16 KB */

int exec_elf_from_path(const char *path)
{
    serial_write_string("exec: opening '");
    serial_write_string((char *)path);
    serial_write_string("'\n");

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        serial_write_string("exec: open failed\n");
        return -1;
    }

    /* Use PMM directly for the ELF buffer — kheap can't serve multi-page
     * allocations because coalescing stops at page boundaries. */
    #define ELF_BUF_PAGES  ((ELF_LOAD_MAX + PAGE_SIZE - 1) / PAGE_SIZE)
    uint8_t *buf = (uint8_t *)pmm_alloc_contiguous_pages(ELF_BUF_PAGES);
    if (!buf) {
        serial_write_string("exec: no memory for ELF buffer\n");
        vfs_close(fd);
        return -1;
    }

    uint32_t total = 0;
    int n;
    while (total < ELF_LOAD_MAX) {
        n = vfs_read(fd, buf + total, ELF_LOAD_MAX - total);
        if (n <= 0) break;
        total += (uint32_t)n;
    }
    vfs_close(fd);

    serial_write_string("exec: read ");
    serial_write_hex(total);
    serial_write_string(" bytes\n");

    if (total == 0) {
        serial_write_string("exec: empty file\n");
        return -1;
    }

    uint32_t entry = elf_load(buf, total);
    if (entry == 0) {
        serial_write_string("exec: ELF load failed\n");
        return -1;
    }

    uint32_t user_esp = elf_setup_user_stack(path);
    if (user_esp == 0) {
        serial_write_string("exec: stack setup failed\n");
        return -1;
    }

    task_t *t = task_create_user(entry, 'X', user_esp);

    if (!t) {
        serial_write_string("exec: task creation failed\n");
        return -1;
    }

    serial_write_string("exec: launched task ");
    serial_write_hex(t->id);
    serial_write_string("\n");

    return (int)t->id;
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

    /* ── Phase 10: Map framebuffer + 2D drawing test ── */
    if (g_framebuffer.addr != 0) {
        uint32_t fb_bytes = g_framebuffer.height * g_framebuffer.pitch;
        serial_write_string("\n=== Phase 10: 2D Primitives + Font ===\n");
        paging_map_phys_range(g_framebuffer.addr, fb_bytes);

        /* Background fill */
        uint32_t bg = make_color(0x20, 0x20, 0x30);
        fill_rect(0, 0, g_framebuffer.width - 1, g_framebuffer.height - 1, bg);

        /* 1. Yellow filled rectangle + red border */
        uint32_t yellow = make_color(0xFF, 0xFF, 0x00);
        uint32_t red    = make_color(0xFF, 0x00, 0x00);
        fill_rect(400, 300, 200, 100, yellow);
        draw_rect(396, 296, 208, 108, red);

        /* 2. Cyan diagonal line */
        uint32_t cyan = make_color(0x00, 0xFF, 0xFF);
        draw_line(100, 100, 900, 650, cyan);

        /* 3. White title text */
        uint32_t white = make_color(0xFF, 0xFF, 0xFF);
        draw_string(50, 30, "Kumo OS - Phase 10", white);

        /* 4. Full printable ASCII character display */
        char ascii_row[100];
        int ci = 0;
        for (int c = 0x20; c <= 0x7E; c++) {
            ascii_row[ci++] = (char)c;
        }
        ascii_row[ci] = '\0';

        uint32_t grey = make_color(0xAA, 0xAA, 0xAA);
        draw_string(50, 60, ascii_row, grey);
        draw_string(50, 78, "abcdefghijklmnopqrstuvwxyz", make_color(0x00, 0xCC, 0x00));
        draw_string(50, 96, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", make_color(0x00, 0x88, 0xFF));
        draw_string(50, 114, "0123456789 !@#$%^&*()[]{}<>", make_color(0xFF, 0x88, 0x00));

        serial_write_string("FB: Phase 10 drawing complete.\n");
    } else {
        serial_write_string("FB: no framebuffer — GRUB did not provide one.\n");
    }

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

    /* ── Interrupts deferred: sti is below after all tasks are created.
     *   If interrupts fire before g_current is valid,
     *   irq_handler → task_yield() would dereference NULL. */

    /* ── Phase 8b Step 1: ELF header parse test ── */
    serial_write_string("\n=== Phase 8b Step 1: ELF parse ===\n");
    elf_parse_and_print(build_hello_elf_elf, build_hello_elf_elf_len);

    /* ── Phase 8b Step 2: ELF memory mapping ── */
    serial_write_string("\n=== Phase 8b Step 2: ELF load ===\n");
    uint32_t elf_entry = elf_load(build_hello_elf_elf, build_hello_elf_elf_len);
    if (elf_entry == 0) {
        serial_write_string("ERROR: ELF load failed!\n");
    }
    (void)elf_entry;   /* used in Step 4 for task_create_user */

    /* ── Phase 8b Step 3: user stack setup (argc/argv) ── */
    serial_write_string("\n=== Phase 8b Step 3: Stack setup ===\n");
    uint32_t user_esp = elf_setup_user_stack("hello_elf");
    if (user_esp == 0) {
        serial_write_string("ERROR: stack setup failed!\n");
    }
    (void)user_esp;

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

    /* ── Pre-load ELF test program into ramfs (must be after task_init:
     *     vfs_open/vfs_write need task_current() for the fd table) ── */
    serial_write_string("\n=== Pre-load /hello.elf into ramfs ===\n");
    {
        int elffd = vfs_open("/hello.elf", O_WRONLY);
        if (elffd >= 0) {
            int written = vfs_write(elffd, build_hello_elf_elf, build_hello_elf_elf_len);
            serial_write_string("Wrote ");
            serial_write_hex((uint32_t)written);
            serial_write_string(" bytes to /hello.elf\n");
            vfs_close(elffd);
        } else {
            serial_write_string("ERROR: could not create /hello.elf\n");
        }
    }

    /* ── Phase 8b Step 4: create ELF task with pre-built user stack ── */
    serial_write_string("\n=== Phase 8b Step 4: ELF task create ===\n");
    serial_write_string("ELF: entry=");
    serial_write_hex(elf_entry);
    serial_write_string(" user_esp=");
    serial_write_hex(user_esp);
    serial_write_string("\n");

    /* Verify combined stack frame via serial before execution */
    task_t *elf_task = task_create_user(elf_entry, 'E', user_esp);
    if (elf_task) {
        uint32_t *kst = (uint32_t *)(elf_task->esp);
        serial_write_string("ELF task kstack frame (10 dwords):\n");
        for (int i = 0; i < 10; i++) {
            serial_write_string("  sp[");
            serial_write_hex(i);
            serial_write_string("] = ");
            serial_write_hex(kst[i]);
            serial_write_string("\n");
        }
        serial_write_string("  -> iret will pop: EIP=");
        serial_write_hex(kst[5]);
        serial_write_string(" CS=");
        serial_write_hex(kst[6]);
        serial_write_string(" EFLAGS=");
        serial_write_hex(kst[7]);
        serial_write_string(" ESP=");
        serial_write_hex(kst[8]);
        serial_write_string(" SS=");
        serial_write_hex(kst[9]);
        serial_write_string("\n");
    }

    task_create_user(shell_page, 'S', 0);   /* 'S' = Shell */

    /* ── Phase 11: Preemptive scheduling — register-verification tasks ──
     * Two tasks with distinct 6-register markers.  Each verifies its
     * own registers across cooperative yields AND timer preemptions.
     * If preemption corrupts a register, FAIL_A or FAIL_B prints. */
    serial_write_string("\n=== Phase 11: Regtest tasks ===\n");
    {
        uint32_t rta_page = pmm_alloc_page();
        paging_set_user_accessible(rta_page);
        copy_code(rta_page, build_regtest_a_bin, build_regtest_a_bin_len);
        task_t *rta = task_create_user(rta_page, 'a', 0);
        serial_write_string("Regtest A: task ");
        serial_write_hex(rta ? rta->id : 0);
        serial_write_string("\n");

        uint32_t rtb_page = pmm_alloc_page();
        paging_set_user_accessible(rtb_page);
        copy_code(rtb_page, build_regtest_b_bin, build_regtest_b_bin_len);
        task_t *rtb = task_create_user(rtb_page, 'b', 0);
        serial_write_string("Regtest B: task ");
        serial_write_hex(rtb ? rtb->id : 0);
        serial_write_string("\n");
    }

    serial_write_string("Shell: enabling interrupts (sti) + entering idle/scheduler loop...\n");
    __asm__ volatile("sti");
    for (;;) {
        task_yield();
    }
}
