#ifndef SCHED_TASK_H
#define SCHED_TASK_H

#include <stdint.h>

/* ── Task states ── */
#define TASK_READY    0
#define TASK_RUNNING  1

/* ── TCB (Task Control Block) ──
 *
 * esp MUST be the first field — switch.asm writes to offset 0. */

#define MAX_FD_PER_TASK 16

/* Forward declaration — full definition in fs/vfs.h */
typedef struct vfs_file vfs_file_t;

typedef struct task {
    uint32_t esp;              /* offset 0x00 — saved stack pointer */
    uint32_t id;               /* offset 0x04 */
    uint32_t state;            /* offset 0x08 */
    struct task *next;         /* offset 0x0C — circular linked list */
    uint32_t kernel_stack_top; /* offset 0x10 — top of 2-page kernel stack (for TSS.esp0) */
    vfs_file_t *fd_table[MAX_FD_PER_TASK]; /* offset 0x14+ — per-task file descriptors */
} task_t;

/* ── Scheduler API ── */

void    task_init(void);
task_t *task_create(void (*entry)(void));
task_t *task_create_user(uint32_t entry_addr, uint32_t id_char);
void    task_yield(void);
task_t *task_current(void);

/* switch.asm — saves current → tcb[0], loads next → esp, pops callee-saved, ret */
extern void switch_to(task_t *current, task_t *next);

#endif
