#ifndef SCHED_TASK_H
#define SCHED_TASK_H

#include <stdint.h>

/* ── Task states ── */
#define TASK_READY    0
#define TASK_RUNNING  1

/* ── TCB (Task Control Block) ──
 *
 * esp MUST be the first field — switch.asm writes to offset 0. */
typedef struct task {
    uint32_t esp;           /* offset 0x00 — saved stack pointer */
    uint32_t id;            /* offset 0x04 */
    uint32_t state;         /* offset 0x08 */
    struct task *next;      /* offset 0x0C — circular linked list */
} task_t;

/* ── Scheduler API ── */

void    task_init(void);
task_t *task_create(void (*entry)(void));
void    task_yield(void);
task_t *task_current(void);

/* switch.asm — saves current → tcb[0], loads next → esp, pops callee-saved, ret */
extern void switch_to(task_t *current, task_t *next);

#endif
