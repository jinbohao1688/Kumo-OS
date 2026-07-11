#include "kheap.h"
#include "pmm.h"
#include "../drivers/serial.h"
#include <stddef.h>

/* ── Constants ── */

#define HEADER_SIZE      8           /* size(4) + free(4) */
#define MIN_BLOCK_SIZE   16          /* header + room for next/prev ptrs */
#define ALIGN            8
#define HEAP_INIT_PAGES  16          /* 64 KB initial heap */
#define MAX_HEAP_PAGES   256         /* 1 MB max heap */

/* ── Block header (8 bytes, placed before every block) ── */

typedef struct block_header {
    uint32_t size;       /* total block size (header + data), 8-byte aligned */
    uint32_t free;       /* 1 = free, 0 = in use */
} block_header_t;

/* ── Free-list helpers ──
 * When a block is free, its data area stores:
 *   offset 0: next free block ptr (4 bytes)
 *   offset 4: prev free block ptr (4 bytes)  */

static block_header_t *free_head;

static block_header_t **next_ptr(block_header_t *b) {
    return (block_header_t **)((uint8_t *)b + HEADER_SIZE);
}
static block_header_t **prev_ptr(block_header_t *b) {
    return (block_header_t **)((uint8_t *)b + HEADER_SIZE + 4);
}

static void fl_insert(block_header_t *b)
{
    b->free = 1;
    *next_ptr(b) = free_head;
    *prev_ptr(b) = NULL;
    if (free_head) *prev_ptr(free_head) = b;
    free_head = b;
}

static void fl_remove(block_header_t *b)
{
    block_header_t *n = *next_ptr(b);
    block_header_t *p = *prev_ptr(b);
    if (p) *next_ptr(p) = n;
    else   free_head = n;
    if (n) *prev_ptr(n) = p;
}

/* ── Heap page tracking── */

static uint32_t heap_pages[MAX_HEAP_PAGES];
static uint32_t heap_page_count;

static inline uint32_t page_of(void *p) {
    return (uint32_t)p & ~(PAGE_SIZE - 1);
}

/* ── Forward coalescing ──
 * If the block physically after `b` (within the same page) is free,
 * absorb it into `b`. */

static void coalesce_forward(block_header_t *b)
{
    uint32_t ps   = page_of(b);
    uint32_t pend = ps + PAGE_SIZE;
    block_header_t *next = (block_header_t *)((uint8_t *)b + b->size);

    if ((uint32_t)next >= pend) return;
    if (!next->free)            return;

    fl_remove(next);
    b->size += next->size;
}

/* ── Backward coalescing ──
 * Walk from page start through the size chain to find the block
 * immediately before `b`.  If it is free, it absorbs `b`.
 * Returns 1 if merge happened, 0 otherwise. */

static int coalesce_backward(block_header_t *b)
{
    uint32_t ps = page_of(b);
    if ((uint32_t)b == ps) return 0;

    block_header_t *prev = NULL;
    block_header_t *curr = (block_header_t *)ps;

    while ((uint32_t)curr < (uint32_t)b) {
        prev = curr;
        curr = (block_header_t *)((uint8_t *)curr + curr->size);
    }

    if (!prev || !prev->free) return 0;

    /* prev absorbs b.  prev is already in the free list — just grow it. */
    prev->size += b->size;
    return 1;
}

/* ── Heap expansion ──
 * Allocates 4 contiguous pages (16 KB) at a time so the free list
 * contains blocks large enough for multi-KB allocations. */

#define EXPAND_PAGES 4

static int heap_expand(void)
{
    if (heap_page_count + EXPAND_PAGES > MAX_HEAP_PAGES) return 0;

    uint32_t base = pmm_alloc_contiguous_pages(EXPAND_PAGES);
    if (!base) return 0;

    block_header_t *block = (block_header_t *)base;
    block->size = EXPAND_PAGES * PAGE_SIZE;
    fl_insert(block);

    for (uint32_t i = 0; i < EXPAND_PAGES; i++)
        heap_pages[heap_page_count++] = base + i * PAGE_SIZE;

    serial_write_string("kheap: expanded, base ");
    serial_write_hex(base);
    serial_write_string(" (");
    serial_write_hex(EXPAND_PAGES);
    serial_write_string(" pages)\n");

    return 1;
}

/* ── Public API ── */

void kheap_init(void)
{
    free_head = NULL;
    heap_page_count = 0;

    serial_write_string("KHeap: allocating ");
    serial_write_hex(HEAP_INIT_PAGES);
    serial_write_string(" initial pages...\n");

    /* Allocate all initial pages contiguously → one large block.
     * Per-page allocation + per-page free list entries can't serve
     * kmalloc requests larger than PAGE_SIZE because coalescing
     * doesn't cross page boundaries. */
    uint32_t base = pmm_alloc_contiguous_pages(HEAP_INIT_PAGES);
    if (!base) {
        serial_write_string("KHeap: ERROR — PMM out of memory\n");
        return;
    }
    block_header_t *block = (block_header_t *)base;
    block->size = HEAP_INIT_PAGES * PAGE_SIZE;
    fl_insert(block);

    for (uint32_t i = 0; i < HEAP_INIT_PAGES; i++)
        heap_pages[heap_page_count++] = base + i * PAGE_SIZE;

    uint32_t total = HEAP_INIT_PAGES * PAGE_SIZE;
    serial_write_string("KHeap: init done, ");
    serial_write_hex(HEAP_INIT_PAGES);
    serial_write_string(" pages (");
    serial_write_hex(total);
    serial_write_string(" bytes)\n");
}

void *kmalloc(uint32_t size)
{
    uint32_t aligned = (size + ALIGN - 1) & ~(ALIGN - 1);
    uint32_t needed  = aligned + HEADER_SIZE;
    if (needed < MIN_BLOCK_SIZE) needed = MIN_BLOCK_SIZE;

    /* First-fit */
    block_header_t *b = free_head;
    while (b) {
        if (b->size >= needed) {
            if (b->size - needed >= MIN_BLOCK_SIZE) {
                /* Split */
                block_header_t *r =
                    (block_header_t *)((uint8_t *)b + needed);
                r->size = b->size - needed;
                r->free = 1;

                block_header_t *n = *next_ptr(b);
                block_header_t *p = *prev_ptr(b);
                *next_ptr(r) = n;
                *prev_ptr(r) = p;
                if (p) *next_ptr(p) = r;
                else   free_head = r;
                if (n) *prev_ptr(n) = r;

                b->size = needed;
            } else {
                fl_remove(b);
            }

            b->free = 0;
            return (uint8_t *)b + HEADER_SIZE;
        }
        b = *next_ptr(b);
    }

    if (!heap_expand()) return NULL;
    return kmalloc(size);
}

void kfree(void *ptr)
{
    if (!ptr) return;

    block_header_t *b = (block_header_t *)ptr - 1;

    if (b->free) {
        serial_write_string("kfree: double free at 0x");
        serial_write_hex((uint32_t)ptr);
        serial_write_string("\n");
        return;
    }

    b->free = 1;

    /* 1. Forward: b absorbs any adjacent free block after it */
    coalesce_forward(b);

    /* 2. Backward: the block before b absorbs b (if free).
     *    If merge happens, prev is already in free list.
     *    If not, insert b into free list. */
    if (!coalesce_backward(b))
        fl_insert(b);
}
