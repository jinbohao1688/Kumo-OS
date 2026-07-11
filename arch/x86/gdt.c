#include "gdt.h"
#include "../../drivers/serial.h"

/* Flat-model GDT: 6 entries — null, kernel code, kernel data,
   user code, user data, TSS.
   All segments base=0, limit=4GB, so segmentation is effectively
   transparent. Real memory isolation is done by paging (Phase 3). */

#define GDT_SIZE 6
static gdt_entry_t gdt[GDT_SIZE];
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

/* Access byte bit layout:
   bit 7      P  (Present)           = 1
   bits 6:5   DPL (Descriptor Priv)  = 00 (kernel) or 11 (user)
   bit 4      S  (system)            = 1 (code/data segment)
   bits 3:0   Type
     Code:  Exec(1) | Conforming(0) | Readable(1) | Accessed(0) = 1010 = 0xA
     Data:  Exec(0) | Direction(0)  | Writable(1) | Accessed(0) = 0010 = 0x2

   Kernel code: 1_00_1_1010 = 0x9A
   Kernel data: 1_00_1_0010 = 0x92
   User code:   1_11_1_1010 = 0xFA
   User data:   1_11_1_0010 = 0xF2            */

/* Granularity byte fields:
   bit 7      G  (Granularity)       = 1 (limit in 4KB pages → 4GB)
   bit 6      D/B (32-bit operand)   = 1
   bit 5      L  (long mode)         = 0
   bit 4      AVL                    = 0
   bits 3:0   limit_high (top 4 bits of 20-bit limit)
   → 0xCF */

#define GDT_ACCESS_KCODE  0x9A
#define GDT_ACCESS_KDATA  0x92
#define GDT_ACCESS_UCODE  0xFA
#define GDT_ACCESS_UDATA  0xF2
#define GDT_FLAT_GRAN     0xCF

static void load_gdtr(void)
{
    gdt_ptr.limit = sizeof(gdt) - 1;   /* 6 * 8 - 1 = 47 */
    gdt_ptr.base  = (uint32_t)&gdt;

    __asm__ volatile(
        "lgdt %0\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        "pushl $0x08\n"
        "pushl $1f\n"
        "lret\n"
        "1:\n"
        :
        : "m"(gdt_ptr)
        : "eax", "memory"
    );
}

void gdt_init(void)
{
    /* Entry 0: null descriptor — required by CPU, accessing it → #GP */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Entry 1: kernel code segment (selector KERNEL_CS = 0x08) */
    gdt_set_entry(1, 0, 0xFFFFF, GDT_ACCESS_KCODE, GDT_FLAT_GRAN);

    /* Entry 2: kernel data segment (selector KERNEL_DS = 0x10) */
    gdt_set_entry(2, 0, 0xFFFFF, GDT_ACCESS_KDATA, GDT_FLAT_GRAN);

    /* Entry 3: user code segment (selector USER_CS = 0x1B = 0x18|3)
       DPL=3, exec+read, non-conforming, access=0xFA */
    gdt_set_entry(3, 0, 0xFFFFF, GDT_ACCESS_UCODE, GDT_FLAT_GRAN);

    /* Entry 4: user data segment (selector USER_DS = 0x23 = 0x20|3)
       DPL=3, read+write, expand-up, access=0xF2 */
    gdt_set_entry(4, 0, 0xFFFFF, GDT_ACCESS_UDATA, GDT_FLAT_GRAN);

    /* Entry 5: TSS — left blank for now.  tss_init() fills it via gdt_set_tss(). */
    gdt_set_entry(5, 0, 0, 0, 0);

    load_gdtr();

    serial_write_string("GDT: 5 segments + TSS placeholder, GDTR loaded.\n");
}

/* Called by tss_init() to fill GDT[5] with the TSS descriptor and reload GDTR. */
void gdt_set_tss(uint32_t base, uint32_t limit)
{
    gdt[5].limit_low   = limit & 0xFFFF;
    gdt[5].base_low    = base & 0xFFFF;
    gdt[5].base_mid    = (base >> 16) & 0xFF;
    gdt[5].access      = 0x89;   /* P=1 DPL=0 S=0 Type=1001 (32-bit avl TSS) */
    gdt[5].granularity = 0x00;   /* byte-granularity, limit_high=0 */
    gdt[5].base_high   = (base >> 24) & 0xFF;

    load_gdtr();

    serial_write_string("GDT: TSS descriptor set (idx=5, sel=0x28), GDTR reloaded.\n");
}
