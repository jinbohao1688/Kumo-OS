#include "isr.h"
#include "../../drivers/serial.h"
#include "../../sched/task.h"

static const char *exception_names[32] = {
    "#DE Divide Error",
    "#DB Debug",
    "NMI",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR BOUND Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection Fault",
    "#PF Page Fault",
    "Reserved (15)",
    "#MF x87 FPU Error",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD FP Exception",
    "#VE Virtualization Exception",
    "#CP Control Protection Exception",
    "Reserved (22)", "Reserved (23)", "Reserved (24)", "Reserved (25)",
    "Reserved (26)", "Reserved (27)", "Reserved (28)", "Reserved (29)",
    "Reserved (30)", "Reserved (31)"
};

void isr_handler(registers_t *r)
{
    serial_write_string("\n");
    serial_write_string("=== EXCEPTION ===\n");

    if (r->int_no < 32) {
        serial_write_string("  ");
        serial_write_string(exception_names[r->int_no]);
        serial_write_string("\n");
    }

    serial_write_string("  Vector:  ");
    serial_write_hex(r->int_no);
    serial_write_string("\n");

    serial_write_string("  ErrCode: ");
    serial_write_hex(r->err_code);
    serial_write_string("\n");

    /* Read CR2 (faulting linear address) */
    uint32_t cr2_val;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2_val));
    serial_write_string("  CR2:     ");
    serial_write_hex(cr2_val);
    serial_write_string("\n");

    /* Identify which task triggered the fault */
    task_t *cur = task_current();
    if (cur) {
        serial_write_string("  Task:    id=");
        serial_write_hex(cur->id);
        serial_write_string(" cr3=");
        serial_write_hex(cur->cr3);
        serial_write_string("\n");
    }

    serial_write_string("  EIP:     ");
    serial_write_hex(r->eip);
    serial_write_string("  CS: ");
    serial_write_hex(r->cs);
    serial_write_string("  EFLAGS: ");
    serial_write_hex(r->eflags);
    serial_write_string("\n");

    serial_write_string("  EAX="); serial_write_hex(r->eax);
    serial_write_string("  EBX="); serial_write_hex(r->ebx);
    serial_write_string("  ECX="); serial_write_hex(r->ecx);
    serial_write_string("  EDX="); serial_write_hex(r->edx);
    serial_write_string("\n");

    serial_write_string("  ESI="); serial_write_hex(r->esi);
    serial_write_string("  EDI="); serial_write_hex(r->edi);
    serial_write_string("  EBP="); serial_write_hex(r->ebp);
    serial_write_string("  ESP="); serial_write_hex(r->old_esp);
    serial_write_string("\n");

    serial_write_string("=== HALT ===\n");

    /* Don't attempt to recover — halt forever.
       At this stage we only verify the exception was caught. */
    for (;;) {
        __asm__ volatile("hlt");
    }
}
