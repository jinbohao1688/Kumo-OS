#ifndef ARCH_X86_IDT_H
#define ARCH_X86_IDT_H

#include <stdint.h>

/* 8-byte interrupt gate descriptor.
   Layout per Intel SDM Vol.3 §6.11:
   - offset_low  (16 bits) — handler address bits 15:0
   - selector    (16 bits) — code segment selector in GDT
   - reserved    (8 bits)  — must be 0
   - type_attr   (8 bits)  — P|DPL|0|Gate Type
   - offset_high (16 bits) — handler address bits 31:16

   The 32-bit offset is split because this format descends from
   the 286's 16-bit gate descriptor; Intel kept the layout for
   backward compatibility when adding 32-bit support in the 386. */

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  reserved;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

/* IDTR register value: 16-bit limit + 32-bit base address.
   Passed to the lidt instruction. */

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

void idt_init(void);

/* Set an individual gate — used by irq_init() to wire IRQ handlers
   into vectors 32-47 after idt_init() has already loaded the table. */
void idt_set_gate(int vector, uint32_t handler_addr,
                  uint16_t selector, uint8_t type_attr);

#endif
