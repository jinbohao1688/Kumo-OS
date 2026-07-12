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

/* Map a physical address range [phys_addr, phys_addr+size) into the identity-mapped
 * kernel address space. Creates page tables on demand if the range falls outside
 * the initial paging_init coverage. Kernel-only mapping (supervisor, r/w). */
void paging_map_phys_range(uint32_t phys_addr, uint32_t size);

/* ADR-003: Unmap virtual address 0 (NULL page) — clear PTE[0].P + invlpg. */
void paging_unmap_null_page(void);

/* ADR-004: Check whether [vaddr, vaddr+size) lies entirely within user-accessible pages.
 * Returns 1 if all pages in the range have PDE.U/S=1 + PTE.U/S=1 + PTE.P=1. */
int is_user_accessible_range(const void *vaddr, uint32_t size);

/* ADR-004: Copy data from user space to kernel space.
 * Validates the entire [user_src, user_src+size) range before copying.
 * Returns 0 on success, -1 if any page in the range is not user-accessible. */
uint32_t copy_from_user(void *kernel_dst, const void *user_src, uint32_t size);

/* ADR-004: Copy data from kernel space to user space.
 * Validates the entire [user_dst, user_dst+size) range before copying.
 * Returns 0 on success, -1 if any page in the range is not user-accessible. */
uint32_t copy_to_user(void *user_dst, const void *kernel_src, uint32_t size);

/* ADR-004: Copy a NUL-terminated string from user space to kernel buffer.
 * Byte-by-byte scan: validates a page only when crossing a 4KB boundary,
 * stops at NUL. Never touches pages beyond the string.
 * Returns 0 on success (NUL found within max_len), -1 on failure. */
uint32_t copy_from_user_string(char *kbuf, const char *user_ptr, uint32_t max_len);

/* ── Phase 12: per-task page directory isolation ── */

/* Clone the kernel page directory.  All PDEs are copied (share kernel PTs).
 * User pages are later marked via paging_set_user_accessible_for_task(),
 * which clones individual PTs on demand.
 * Returns the physical address of the new page directory, or 0 on failure. */
uint32_t paging_clone_kernel_pd(void);

/* Mark a physical page as user-accessible (U/S=1) in a specific task's PD.
 * If the PDE covering phys_addr still points to a shared kernel PT,
 * clones that PT first (allocates a private copy), then sets PTE.U/S=1
 * and PDE.U/S=1 on the task-private entries.
 * Must be called before the task runs for the first time (no invlpg needed). */
void paging_set_user_accessible_for_task(uint32_t task_pd_phys, uint32_t phys_addr);

#endif
