#ifndef ARCH_X86_ISR_H
#define ARCH_X86_ISR_H

#include <stdint.h>

/* Stack layout the trampoline produces (from ESP upward).
   The order MUST exactly match pushad + int_no + err_code + iret frame.

   pushad order (Intel SDM Vol.2): EAX(high) → ECX → EDX → EBX
                                   → old_ESP → EBP → ESI → EDI(low)

   Below pushad:
     int_no    — our trampoline
     err_code  — CPU or dummy 0
     EIP/CS/EFLAGS — CPU iret frame                          */

typedef struct {
    uint32_t edi;       /* ESP+0  — pushad: last pushed     */
    uint32_t esi;       /* ESP+4                            */
    uint32_t ebp;       /* ESP+8                            */
    uint32_t old_esp;   /* ESP+12 — from pusha, original ESP */
    uint32_t ebx;       /* ESP+16                           */
    uint32_t edx;       /* ESP+20                           */
    uint32_t ecx;       /* ESP+24                           */
    uint32_t eax;       /* ESP+28 — pushad: first pushed    */
    uint32_t int_no;    /* ESP+32 — vector number           */
    uint32_t err_code;  /* ESP+36 — error code or dummy 0    */
    uint32_t eip;       /* ESP+40 — iret frame              */
    uint32_t cs;        /* ESP+44                           */
    uint32_t eflags;    /* ESP+48                           */
} registers_t;

/* Extern the 32 per-vector entry points (defined in isr.asm).
   Used by idt.c to wire them into the IDT. */

extern void isr_0(void);
extern void isr_1(void);
extern void isr_2(void);
extern void isr_3(void);
extern void isr_4(void);
extern void isr_5(void);
extern void isr_6(void);
extern void isr_7(void);
extern void isr_8(void);
extern void isr_9(void);
extern void isr_10(void);
extern void isr_11(void);
extern void isr_12(void);
extern void isr_13(void);
extern void isr_14(void);
extern void isr_15(void);
extern void isr_16(void);
extern void isr_17(void);
extern void isr_18(void);
extern void isr_19(void);
extern void isr_20(void);
extern void isr_21(void);
extern void isr_22(void);
extern void isr_23(void);
extern void isr_24(void);
extern void isr_25(void);
extern void isr_26(void);
extern void isr_27(void);
extern void isr_28(void);
extern void isr_29(void);
extern void isr_30(void);
extern void isr_31(void);

#endif
