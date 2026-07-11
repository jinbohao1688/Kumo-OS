#ifndef ARCH_X86_TSS_H
#define ARCH_X86_TSS_H

#include <stdint.h>

/* 32-bit TSS — only the fields we need for ring0↔ring3 stack switching.
   Layout per Intel SDM Vol.3 §8.7.
   We only use ss0/esp0; hardware task switching is NOT used. */

typedef struct {
    uint32_t prev_tss;   /* 0x00 — unused (software link) */
    uint32_t esp0;       /* 0x04 — ring0 stack pointer */
    uint32_t ss0;        /* 0x08 — ring0 stack segment (KERNEL_DS = 0x10) */
    uint32_t esp1;       /* 0x0C — unused */
    uint32_t ss1;        /* 0x10 — unused */
    uint32_t esp2;       /* 0x14 — unused */
    uint32_t ss2;        /* 0x18 — unused */
    uint32_t cr3;        /* 0x1C — unused (we manage CR3 manually) */
    uint32_t eip;        /* 0x20 — unused */
    uint32_t eflags;     /* 0x24 — unused */
    uint32_t eax;        /* 0x28 — unused */
    uint32_t ecx;        /* 0x2C — unused */
    uint32_t edx;        /* 0x30 — unused */
    uint32_t ebx;        /* 0x34 — unused */
    uint32_t esp;        /* 0x38 — unused */
    uint32_t ebp;        /* 0x3C — unused */
    uint32_t esi;        /* 0x40 — unused */
    uint32_t edi;        /* 0x44 — unused */
    uint32_t es;         /* 0x48 — unused */
    uint32_t cs;         /* 0x4C — unused */
    uint32_t ss;         /* 0x50 — unused */
    uint32_t ds;         /* 0x54 — unused */
    uint32_t fs;         /* 0x58 — unused */
    uint32_t gs;         /* 0x5C — unused */
    uint32_t ldt;        /* 0x60 — unused */
    uint16_t trap;       /* 0x64 — unused */
    uint16_t iomap_base; /* 0x66 — offset to I/O permission bitmap from TSS base */
} __attribute__((packed)) tss_t;

void tss_init(void);
void tss_set_esp0(uint32_t esp);

#endif
