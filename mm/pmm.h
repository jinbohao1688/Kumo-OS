#ifndef MM_PMM_H
#define MM_PMM_H

#include <stdint.h>

#define PAGE_SIZE 4096

void     pmm_init(void);
uint32_t pmm_alloc_page(void);   /* returns physical address, 0 if out of memory */
uint32_t pmm_alloc_contiguous_pages(uint32_t count);  /* count consecutive pages */
uint32_t pmm_free_page_count(void);

#endif
