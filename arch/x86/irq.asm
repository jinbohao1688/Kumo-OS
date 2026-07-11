section .text

; ── IRQ handler entry points ──
;
; IRQs differ from CPU exceptions in one critical way: the handler
; MUST send an EOI to the PIC before returning (via pic_send_eoi).
; This is done in irq_handler() in irq.c.
;
; The trampoline structure is otherwise identical to isr.asm:
;   1. Push dummy error code (IRQs never push one)
;   2. Push vector number
;   3. pushad → call irq_handler → popad → add esp,8 → iret

global irq0_entry
irq0_entry:
    push 0          ; dummy error code
    push 32         ; vector 32 = IRQ0 (timer)
    jmp irq_common_stub

; ── Common IRQ stub ──
;
; Separate from isr_common_stub because it calls irq_handler
; (which sends EOI and returns), not isr_handler (which halts).

extern irq_handler

irq_common_stub:
    pushad
    push esp
    call irq_handler
    add esp, 4
    popad
    add esp, 8         ; pop int_no + err_code
    iret
