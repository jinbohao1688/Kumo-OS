#include "gdt.h"
#include "../../drivers/serial.h"

/* Flat-model GDT: 3 entries — null, kernel code, kernel data.
   All segments base=0, limit=4GB, so segmentation is effectively
   transparent. Real memory isolation will be done by paging (Phases 3). */

static gdt_entry_t gdt[3];
static gdt_ptr_t   gdt_ptr;

static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t granularity)
{
    gdt[index].limit_low   = limit & 0xFFFF;
    gdt[index].base_low    = base & 0xFFFF;
    gdt[index].base_mid    = (base >> 16) & 0xFF;
    gdt[index].access      = access;
    gdt[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    gdt[index].base_high   = (base >> 24) & 0xFF;
}

/* Access byte fields:
   bit 7      P  (Present)           = 1
   bits 6:5   DPL (Descriptor Priv)  = 00 (kernel) or 11 (user)
   bit 4      S  (system)            = 1 (code/data segment)
   bits 3:0   Type
     Code:  Exec(1) | Conforming(0) | Readable(1) | Accessed(0) = 1010 = 0xA
     Data:  Exec(0) | Direction(0)  | Writable(1) | Accessed(0) = 0010 = 0x2

   Kernel code: 1_00_1_1010 = 0x9A
   Kernel data: 1_00_1_0010 = 0x92 */

/* Granularity byte fields:
   bit 7      G  (Granularity)       = 1 (limit in 4KB pages → 4GB)
   bit 6      D/B (32-bit operand)   = 1
   bit 5      L  (long mode)         = 0
   bit 4      AVL                    = 0
   bits 3:0   limit_high (top 4 bits of 20-bit limit)
   → 0xCF */

#define GDT_ACCESS_KCODE  0x9A
#define GDT_ACCESS_KDATA  0x92
#define GDT_FLAT_GRAN     0xCF

void gdt_init(void)
{
    /* Entry 0: null descriptor — required by CPU, accessing it → #GP */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Entry 1: kernel code segment (selector 0x08) */
    gdt_set_entry(1, 0, 0xFFFFF, GDT_ACCESS_KCODE, GDT_FLAT_GRAN);

    /* Entry 2: kernel data segment (selector 0x10) */
    gdt_set_entry(2, 0, 0xFFFFF, GDT_ACCESS_KDATA, GDT_FLAT_GRAN);

    /* Build GDTR value */
    gdt_ptr.limit = sizeof(gdt) - 1;   /* 3 * 8 - 1 = 23 */
    gdt_ptr.base  = (uint32_t)&gdt;

    /* Load GDTR, then reload all segment selectors to use the new GDT.
       Data segments (DS/ES/FS/GS/SS) can be reloaded with mov.
       CS must be reloaded via far jump — we use push/lret to simulate
       a far return to the new code segment. */

    __asm__ volatile(
        "lgdt %0\n"                /* load new GDT */
        "movw $0x10, %%ax\n"       /* kernel data selector = GDT[2] */
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"        /* data segments reloaded before far jump */
        "pushl $0x08\n"            /* push new CS = GDT[1] */
        "pushl $1f\n"              /* push return EIP */
        "lret\n"                   /* far return: pops EIP then CS */
        "1:\n"                     /* we land here with new CS */
        :
        : "m"(gdt_ptr)
        : "eax", "memory"
    );

    serial_write_string("GDT initialized.\n");
}
