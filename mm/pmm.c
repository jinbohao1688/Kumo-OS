#include "pmm.h"
#include "multiboot.h"
#include "../drivers/serial.h"

extern uint8_t _kernel_end;       /* from linker.ld — end of .bss */

#define MANAGED_BASE 0x100000     /* 1 MB — start of managed physical memory */

/* Page indices for the kernel/user region boundary.
 * kernel_end_idx = (USER_MEM_START - MANAGED_BASE) / PAGE_SIZE
 * user_start_idx = same value, but semantically "first user page index" */
static uint32_t kernel_end_idx;
static uint32_t user_start_idx;

static uint8_t  *bitmap;
static uint32_t  total_pages;
static uint32_t  free_pages;
static uint32_t  bitmap_pages_count;
static uint32_t  first_free;

void pmm_init(void)
{
    uint32_t top = g_memory_map.top_of_memory;

    serial_write_string("PMM: top of usable memory = ");
    serial_write_hex(top);
    serial_write_string("\n");

    total_pages  = (top - MANAGED_BASE) / PAGE_SIZE;
    kernel_end_idx = (USER_MEM_START - MANAGED_BASE) / PAGE_SIZE;
    user_start_idx = kernel_end_idx;
    uint32_t bitmap_bytes = (total_pages + 7) / 8;
    bitmap_pages_count    = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    uint32_t bitmap_addr = ((uint32_t)&_kernel_end + PAGE_SIZE - 1)
                           & ~(PAGE_SIZE - 1);
    bitmap     = (uint8_t *)bitmap_addr;
    first_free = bitmap_addr + bitmap_pages_count * PAGE_SIZE;

    serial_write_string("PMM: total_pages = ");
    serial_write_hex(total_pages);
    serial_write_string(", bitmap_bytes = ");
    serial_write_hex(bitmap_bytes);
    serial_write_string(", bitmap at ");
    serial_write_hex(bitmap_addr);
    serial_write_string(", first_free = ");
    serial_write_hex(first_free);
    serial_write_string("\n");

    /* Step 1 — mark every page as used */
    for (uint32_t i = 0; i < bitmap_bytes; i++)
        bitmap[i] = 0xFF;

    /* Step 2 — walk usable regions, free pages >= first_free */
    free_pages = 0;

    for (uint32_t r = 0; r < g_memory_map.count; r++) {
        uint32_t base = g_memory_map.regions[r].base;
        uint32_t end  = base + g_memory_map.regions[r].length;

        /* Clamp to managed range */
        if (base < MANAGED_BASE) base = MANAGED_BASE;
        if (end  > top)          end  = top;

        for (uint32_t addr = base; addr < end; addr += PAGE_SIZE) {
            if (addr < first_free)
                continue;       /* kernel / bitmap — stay marked as used */

            uint32_t bit  = (addr - MANAGED_BASE) / PAGE_SIZE;
            bitmap[bit / 8] &= ~(1 << (bit % 8));
            free_pages++;
        }
    }

    serial_write_string("PMM: free pages = ");
    serial_write_hex(free_pages);
    serial_write_string(" (");
    serial_write_hex(free_pages * PAGE_SIZE);
    serial_write_string(" bytes)\n");
}

/* ── Kernel-region allocator (shared PTs, never cloned) ── */

uint32_t pmm_alloc_page(void)
{
    for (uint32_t i = 0; i < kernel_end_idx && i < total_pages; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            bitmap[i / 8] |= (1 << (i % 8));
            free_pages--;
            return MANAGED_BASE + i * PAGE_SIZE;
        }
    }
    return 0;   /* out of memory in kernel region */
}

uint32_t pmm_alloc_contiguous_pages(uint32_t count)
{
    if (count == 0 || count > free_pages) return 0;

    uint32_t consecutive = 0;
    uint32_t start_idx   = 0;

    for (uint32_t i = 0; i < kernel_end_idx && i < total_pages; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            if (consecutive == 0) start_idx = i;
            consecutive++;
            if (consecutive == count) {
                for (uint32_t j = start_idx; j < start_idx + count; j++) {
                    bitmap[j / 8] |= (1 << (j % 8));
                    free_pages--;
                }
                return MANAGED_BASE + start_idx * PAGE_SIZE;
            }
        } else {
            consecutive = 0;
        }
    }
    return 0;   /* not enough contiguous pages in kernel region */
}

/* ── User-region allocator (cloned on demand, per-task isolation) ── */

uint32_t pmm_alloc_user_page(void)
{
    for (uint32_t i = user_start_idx; i < total_pages; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            bitmap[i / 8] |= (1 << (i % 8));
            free_pages--;
            return MANAGED_BASE + i * PAGE_SIZE;
        }
    }
    return 0;   /* out of memory in user region */
}

uint32_t pmm_alloc_contiguous_user_pages(uint32_t count)
{
    if (count == 0 || count > free_pages) return 0;

    uint32_t consecutive = 0;
    uint32_t start_idx   = 0;

    for (uint32_t i = user_start_idx; i < total_pages; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            if (consecutive == 0) start_idx = i;
            consecutive++;
            if (consecutive == count) {
                for (uint32_t j = start_idx; j < start_idx + count; j++) {
                    bitmap[j / 8] |= (1 << (j % 8));
                    free_pages--;
                }
                return MANAGED_BASE + start_idx * PAGE_SIZE;
            }
        } else {
            consecutive = 0;
        }
    }
    return 0;   /* not enough contiguous pages in user region */
}

uint32_t pmm_free_page_count(void)
{
    return free_pages;
}
