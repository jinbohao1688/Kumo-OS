#include "tss.h"
#include "gdt.h"
#include "../../drivers/serial.h"

static tss_t g_tss;

void tss_init(void)
{
    uint8_t *p = (uint8_t *)&g_tss;
    for (uint32_t i = 0; i < sizeof(g_tss); i++)
        p[i] = 0;

    g_tss.ss0        = KERNEL_DS;
    g_tss.esp0       = 0;             /* caller sets via tss_set_esp0() */
    g_tss.iomap_base = sizeof(tss_t); /* no I/O bitmap → all ports denied */

    /* Install TSS descriptor at GDT[5] (selector = 0x28) */
    gdt_set_tss((uint32_t)&g_tss, sizeof(tss_t) - 1);

    /* Load task register */
    __asm__ volatile("ltr %%ax" : : "a"(0x28));

    serial_write_string("TSS: ltr loaded, ss0=0x10 (KERNEL_DS)\n");
}

void tss_set_esp0(uint32_t esp)
{
    g_tss.esp0 = esp;
}
