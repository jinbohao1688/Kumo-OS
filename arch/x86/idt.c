#include "idt.h"
#include "isr.h"
#include "../../drivers/serial.h"

/* Selector for kernel code segment (GDT[1]). */
#define KERNEL_CS 0x08

/* Interrupt Gate: P=1, DPL=0, Type=0xE (32-bit interrupt gate).
   IF is cleared automatically on entry — safe default for all vectors. */
#define IDT_FLAG_INTR_GATE 0x8E

static idt_entry_t idt[256];
static idt_ptr_t   idt_ptr;

void idt_set_gate(int vector, uint32_t handler_addr,
                   uint16_t selector, uint8_t type_attr)
{
    idt[vector].offset_low  = handler_addr & 0xFFFF;
    idt[vector].selector    = selector;
    idt[vector].reserved    = 0;
    idt[vector].type_attr   = type_attr;
    idt[vector].offset_high = (handler_addr >> 16) & 0xFFFF;
}

void idt_init(void)
{
    /* Wire vectors 0–31 to the real exception trampolines. */
    void (*exception_handlers[32])(void) = {
        isr_0,  isr_1,  isr_2,  isr_3,  isr_4,  isr_5,  isr_6,  isr_7,
        isr_8,  isr_9,  isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
        isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
        isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31,
    };

    for (int i = 0; i < 256; i++) {
        uint32_t addr;
        if (i < 32) {
            addr = (uint32_t)exception_handlers[i];
        } else {
            /* Vectors 32+ keep the placeholder — only wired when
               PIC remap + IRQ handlers are added in a later step. */
            extern void isr_stub(void);
            addr = (uint32_t)&isr_stub;
        }
        idt_set_gate(i, addr, KERNEL_CS, IDT_FLAG_INTR_GATE);
    }

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr) : "memory");

    serial_write_string("IDT initialized (exception vectors 0-31 wired).\n");
}
