#ifndef ARCH_X86_PAGING_H
#define ARCH_X86_PAGING_H

#include <stdint.h>

/* paging_init() — build identity-mapped page tables covering [0, top_of_memory)
 * and enable paging (CR0.PG).
 *
 * HARD DEPENDENCY: pmm_init() MUST have completed before calling this function.
 * The page directory and all page tables are allocated via pmm_alloc_page().
 * Calling paging_init() before pmm_init() will write garbage to page tables. */
void paging_init(void);

/* Set the User/Supervisor bit (U/S=1) on the PTE for phys_addr.
 * After this call, Ring3 code can access the page at phys_addr
 * (which equals its virtual address under identity mapping). */
void paging_set_user_accessible(uint32_t phys_addr);

#endif
