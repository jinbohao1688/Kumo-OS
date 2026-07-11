#ifndef MM_KHEAP_H
#define MM_KHEAP_H

#include <stdint.h>

/* ── Kernel heap allocator ──
 *
 * Free-list based, block size limited to < PAGE_SIZE (blocks never span pages).
 *
 * Invariant: every heap page starts with a valid block header.
 * Forward coalescing uses address arithmetic (header + size).
 * Backward coalescing walks from page start through the size chain.
 *
 * HARD DEPENDENCY: pmm_init() and paging_init() must complete before kheap_init(). */

void  kheap_init(void);
void *kmalloc(uint32_t size);
void  kfree(void *ptr);

#endif
