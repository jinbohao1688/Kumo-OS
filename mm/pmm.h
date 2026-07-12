#ifndef MM_PMM_H
#define MM_PMM_H

#include <stdint.h>

#define PAGE_SIZE 4096

/* Physical memory layout split (Phase 12: per-task page directories).
 * Kernel region [0x100000, 0x800000): shared PTs, never cloned.
 * User region   [0x800000, top]:      cloned on demand, per-task isolation.
 * Boundary at PDE[2] start (0x800000). */
#define USER_MEM_START  0x800000

void     pmm_init(void);

/* Allocate from kernel region [MANAGED_BASE, USER_MEM_START).
 * For kernel code/data/heap/page-tables/kernel-stacks. */
uint32_t pmm_alloc_page(void);
uint32_t pmm_alloc_contiguous_pages(uint32_t count);

/* Allocate from user region [USER_MEM_START, top_of_memory).
 * For user code/data/stack pages that Ring3 directly accesses. */
uint32_t pmm_alloc_user_page(void);
uint32_t pmm_alloc_contiguous_user_pages(uint32_t count);

uint32_t pmm_free_page_count(void);

#endif
