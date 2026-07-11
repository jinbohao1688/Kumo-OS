#ifndef ARCH_X86_GDT_H
#define ARCH_X86_GDT_H

#include <stdint.h>

/* A single 8-byte GDT segment descriptor.
   Layout per Intel SDM Vol.3 §3.4.5:
   - limit_low  (16 bits)
   - base_low   (16 bits)
   - base_mid   (8 bits)
   - access     (8 bits)  — P|DPL|S|Type
   - granularity (8 bits) — G|D/B|L|AVL|limit_high(4)
   - base_high  (8 bits) */

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

/* GDTR register value: 16-bit limit + 32-bit base address.
   Passed to the lgdt instruction. */

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

void gdt_init(void);

#endif
