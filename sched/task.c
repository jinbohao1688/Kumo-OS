#include "task.h"
#include "../mm/kheap.h"
#include "../mm/pmm.h"
#include "../drivers/serial.h"
#include <stddef.h>

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
    idle->esp   = 0;            /* filled on first switch-out */
    idle->id    = 0;
    idle->state = TASK_RUNNING;
    idle->next  = idle;         /* circular list of 1 */

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

    switch_to(old, new);
}

/* ── task_current ── */

task_t *task_current(void)
{
    return g_current;
}
