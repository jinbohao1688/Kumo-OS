#include "task.h"
#include "../mm/kheap.h"
#include "../mm/pmm.h"
#include "../arch/x86/paging.h"
#include "../arch/x86/tss.h"
#include "../drivers/serial.h"
#include <stddef.h>

extern void return_to_ring3(void);

/* ── Scheduler state ── */

static task_t *g_current;
static uint32_t g_next_id = 1;  /* 0 = idle */

/* ── task_init ──
 * Create the idle task TCB.  idle reuses the boot stack (16 KB in .bss).
 * esp = 0 is a sentinel: it gets filled in on the first task_yield() when
 * kmain's current ESP is saved into idle->esp. */

void task_init(void)
{
    task_t *idle = (task_t *)kmalloc(sizeof(task_t));
    idle->esp              = 0;   /* filled on first switch-out */
    idle->id               = 0;
    idle->state            = TASK_RUNNING;
    idle->kernel_stack_top = 0;   /* idle is a kernel task — no ring3 stack switch */
    idle->next             = idle;
    for (int i = 0; i < MAX_FD_PER_TASK; i++)
        idle->fd_table[i] = NULL;

    g_current = idle;

    serial_write_string("Task: idle (id=0) created\n");
}

/* ── task_create ──
 * Allocate 2 pages (8 KB) for kernel stack, build initial context at stack top,
 * insert into circular list after g_current. */

task_t *task_create(void (*entry)(void))
{
    /* ── Stack (2 pages = 8192 bytes, contiguous) ── */
    uint32_t stack_base = pmm_alloc_contiguous_pages(2);
    if (!stack_base) {
        serial_write_string("Task: ERROR — PMM out of memory for stack\n");
        return NULL;
    }

    uint32_t stack_top = stack_base + 2 * PAGE_SIZE;  /* stack grows down */

    /* Build initial stack frame at stack_top - 20.
     * switch_to pops: ebx, esi, edi, ebp, ret  (5 × 4 = 20 bytes).
     * Write in address order (low → high), matching the pop sequence. */
    uint32_t *sp = (uint32_t *)(stack_top - 20);
    sp[0] = 0;                  /* esp+0x00 → pop ebx */
    sp[1] = 0;                  /* esp+0x04 → pop esi */
    sp[2] = 0;                  /* esp+0x08 → pop edi */
    sp[3] = 0;                  /* esp+0x0C → pop ebp */
    sp[4] = (uint32_t)entry;    /* esp+0x10 → ret (task entry) */

    /* ── TCB ── */
    task_t *t  = (task_t *)kmalloc(sizeof(task_t));
    t->esp    = (uint32_t)sp;
    t->id     = g_next_id++;
    t->state  = TASK_READY;
    for (int i = 0; i < MAX_FD_PER_TASK; i++)
        t->fd_table[i] = NULL;

    /* Insert after g_current in circular list */
    t->next        = g_current->next;
    g_current->next = t;

    serial_write_string("Task: created id=");
    serial_write_hex(t->id);
    serial_write_string(" entry=");
    serial_write_hex((uint32_t)entry);
    serial_write_string(" esp=");
    serial_write_hex(t->esp);
    serial_write_string(" stack_base=");
    serial_write_hex(stack_base);
    serial_write_string("\n");

    return t;
}

/* ── task_create_user ──
 * Create a Ring3 user task.  Allocates:
 *   - 2-page kernel stack (for syscall/interrupt handling)
 *   - 1-page user stack (accessible from Ring3)
 * Builds a combined switch_to + iret frame so that when the scheduler
 * picks this task for the first time, switch_to's `ret` jumps to
 * return_to_ring3, which iret's into user mode. */

task_t *task_create_user(uint32_t entry_addr, uint32_t id_char)
{
    /* ── Kernel stack (2 pages, contiguous) ── */
    uint32_t kstack_base = pmm_alloc_contiguous_pages(2);
    if (!kstack_base) {
        serial_write_string("Task: ERROR — PMM out of memory for kernel stack\n");
        return NULL;
    }
    uint32_t kstack_top = kstack_base + 2 * PAGE_SIZE;

    /* ── User stack (1 page) ── */
    uint32_t ustack_base = pmm_alloc_page();
    if (!ustack_base) {
        serial_write_string("Task: ERROR — PMM out of memory for user stack\n");
        return NULL;
    }
    uint32_t ustack_top = ustack_base + PAGE_SIZE;

    /* Mark user stack page as Ring3-accessible */
    paging_set_user_accessible(ustack_base);

    /* ── Build combined stack frame at kstack_top - 40 ──
     *
     * switch_to pops: ebx, esi, edi, ebp, then `ret` → return_to_ring3.
     * return_to_ring3 does iret which pops: EIP, CS, EFLAGS, ESP, SS.
     *
     * Layout (10 dwords, low addr → high addr):
     *   sp[0] = 0 (ebx)       sp[5] = entry_addr    (iret→EIP)
     *   sp[1] = 0 (esi)       sp[6] = 0x1B          (iret→CS)
     *   sp[2] = 0 (edi)       sp[7] = 0x202         (iret→EFLAGS, IF=1)
     *   sp[3] = 0 (ebp)       sp[8] = ustack_top    (iret→ESP)
     *   sp[4] = return_to_ring3  sp[9] = 0x23       (iret→SS)
     */
    uint32_t *sp = (uint32_t *)(kstack_top - 40);
    sp[0] = 0;
    sp[1] = 0;
    sp[2] = 0;
    sp[3] = 0;
    sp[4] = (uint32_t)&return_to_ring3;
    sp[5] = entry_addr;
    sp[6] = 0x1B;              /* USER_CS */
    sp[7] = 0x202;             /* EFLAGS with IF=1 */
    sp[8] = ustack_top;
    sp[9] = 0x23;              /* USER_DS */

    /* ── TCB ── */
    task_t *t = (task_t *)kmalloc(sizeof(task_t));
    t->esp              = (uint32_t)sp;
    t->id               = g_next_id++;
    t->state            = TASK_READY;
    t->kernel_stack_top = kstack_top;
    for (int i = 0; i < MAX_FD_PER_TASK; i++)
        t->fd_table[i] = NULL;

    /* Insert after g_current in circular list */
    t->next         = g_current->next;
    g_current->next = t;

    serial_write_string("Task: created user id=");
    serial_write_hex(t->id);
    serial_write_string(" id_char=");
    serial_write_hex(id_char);
    serial_write_string(" entry=");
    serial_write_hex(entry_addr);
    serial_write_string(" kstack_top=");
    serial_write_hex(kstack_top);
    serial_write_string(" ustack_top=");
    serial_write_hex(ustack_top);
    serial_write_string("\n");

    return t;
}

/* ── task_yield ──
 * Cooperative yield: save current context, round-robin to next ready task. */

void task_yield(void)
{
    task_t *old = g_current;
    task_t *new = g_current->next;

    if (old == new) return;     /* only one task in system */

    old->state = TASK_READY;
    new->state = TASK_RUNNING;
    g_current  = new;

    /* Update TSS.esp0 so that if the new task is a user task,
     * ring3→ring0 transitions land on its private kernel stack. */
    if (new->kernel_stack_top != 0)
        tss_set_esp0(new->kernel_stack_top);

    switch_to(old, new);
}

/* ── task_current ── */

task_t *task_current(void)
{
    return g_current;
}
