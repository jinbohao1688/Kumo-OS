#include "paging.h"
#include "../../mm/pmm.h"
#include "../../mm/multiboot.h"
#include "../../drivers/serial.h"

#define PDE_COUNT    1024
#define PTE_COUNT    1024
#define PAGE_SIZE    4096
#define PT_COVERAGE  0x400000   /* one page table covers 4 MB */

#define PAGE_P       0x01       /* present */
#define PAGE_RW      0x02       /* read/write */
#define PAGE_DEFAULT (PAGE_P | PAGE_RW)   /* supervisor, r/w, present */

typedef uint32_t pt_entry_t;

static uint32_t pd_phys;       /* physical address of page directory */

/* ── Inline asm: load CR3, set CR0.PG, flush prefetch queue.
 * Breakpoint target for GDB verification — single-step through `mov %%eax, %%cr0`. */
static void paging_load_cr3_and_enable(uint32_t addr)
{
    __asm__ volatile(
        "mov %0, %%cr3\n\t"
        "mov %%cr0, %%eax\n\t"
        "or   $0x80000000, %%eax\n\t"
        "mov %%eax, %%cr0\n\t"
        "jmp  1f\n"
        "1:\n"
        :
        : "b"(addr)            /* pd_phys in ebx — won't clash with eax */
        : "eax", "memory"
    );
}

/* ── paging_init: HARD DEPENDENCY on pmm_init() — PD/PT pages come from PMM. ── */

void paging_init(void)
{
    uint32_t top = g_memory_map.top_of_memory;

    /* How many page tables?  One PT per 4 MB, round up. */
    uint32_t num_pt = (top + PT_COVERAGE - 1) / PT_COVERAGE;

    serial_write_string("Paging: identity-map [0x00000000, ");
    serial_write_hex(top);
    serial_write_string("), ");
    serial_write_hex(num_pt);
    serial_write_string(" page tables\n");

    /* ── 1. Allocate and clear Page Directory ── */
    pd_phys = pmm_alloc_page();
    pt_entry_t *pd = (pt_entry_t *)pd_phys;
    for (int i = 0; i < PDE_COUNT; i++)
        pd[i] = 0;

    serial_write_string("Paging: PD phys = ");
    serial_write_hex(pd_phys);
    serial_write_string("\n");

    /* ── 2. Allocate + fill every Page Table ── */
    uint32_t pt_phys_first = 0;   /* record first PT addr for debug */

    for (uint32_t pt_idx = 0; pt_idx < num_pt; pt_idx++) {
        uint32_t pt_phys = pmm_alloc_page();
        if (pt_idx == 0) pt_phys_first = pt_phys;

        pt_entry_t *pt = (pt_entry_t *)pt_phys;

        /* PDE: point to this PT */
        pd[pt_idx] = pt_phys | PAGE_DEFAULT;

        /* PTE: identity-map every 4 KB page within this 4 MB block */
        for (int pte_idx = 0; pte_idx < PTE_COUNT; pte_idx++) {
            uint32_t phys = pt_idx * PT_COVERAGE + pte_idx * PAGE_SIZE;
            if (phys < top)
                pt[pte_idx] = phys | PAGE_DEFAULT;
            else
                pt[pte_idx] = 0;   /* beyond top_of_memory → not-present */
        }
    }

    serial_write_string("Paging: first PT phys = ");
    serial_write_hex(pt_phys_first);
    serial_write_string(", ");
    serial_write_hex(num_pt);
    serial_write_string(" PTs + 1 PD = ");
    serial_write_hex((num_pt + 1) * PAGE_SIZE);
    serial_write_string(" bytes overhead\n");

    /* ── 3. Enable paging ── */
    serial_write_string("Paging: enabling CR0.PG ...\n");
    paging_load_cr3_and_enable(pd_phys);

    /* If we reach here, the next instruction was fetched successfully
     * through the identity mapping. */
    serial_write_string("Paging: enabled.\n");
}

void paging_set_user_accessible(uint32_t phys_addr)
{
    uint32_t pd_index = phys_addr >> 22;
    uint32_t pt_index = (phys_addr >> 12) & 0x3FF;

    pt_entry_t *pd = (pt_entry_t *)pd_phys;
    if (!(pd[pd_index] & PAGE_P)) return;

    /* The PDE's U/S bit governs the entire 4 MB region covered by this PT.
       Both PDE and PTE must have U/S=1 for user-mode access to succeed. */
    pd[pd_index] |= 0x4;   /* set U/S bit on PDE */

    uint32_t pt_phys = pd[pd_index] & ~0xFFF;    /* PT physical base */
    pt_entry_t *pt   = (pt_entry_t *)pt_phys;     /* identity-mapped: vaddr == paddr */

    pt[pt_index] |= 0x4;   /* set U/S bit on PTE */

    /* Flush TLB for this page */
    __asm__ volatile("invlpg %0" : : "m"(*(volatile uint8_t *)phys_addr));

    serial_write_string("Paging: user-accessible page ");
    serial_write_hex(phys_addr);
    serial_write_string("\n");
}
