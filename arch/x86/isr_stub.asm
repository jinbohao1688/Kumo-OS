section .text
global isr_stub

; Minimal placeholder ISR — does nothing but return.
; All 256 IDT entries point here during the IDT skeleton phase.
; Replaced by real trampolines (save/restore + call C handler)
; once the ISR dispatch step begins.
isr_stub:
    iret
