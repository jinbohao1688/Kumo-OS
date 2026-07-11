#ifndef ARCH_X86_PAGING_H
#define ARCH_X86_PAGING_H

/* paging_init() — build identity-mapped page tables covering [0, top_of_memory)
 * and enable paging (CR0.PG).
 *
 * HARD DEPENDENCY: pmm_init() MUST have completed before calling this function.
 * The page directory and all page tables are allocated via pmm_alloc_page().
 * Calling paging_init() before pmm_init() will write garbage to page tables. */
void paging_init(void);

#endif
