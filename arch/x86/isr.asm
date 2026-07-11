section .text

; ── Macros: generate per-vector entry points ──
;
; ISR_NOERR: exceptions that do NOT push an error code.
;   We push a dummy 0 so the stack layout matches ISR_ERR.
; ISR_ERR:   exceptions that DO push an error code (CPU already did).
;   We only push the vector number.

%macro ISR_NOERR 1
global isr_%1
isr_%1:
    push 0          ; dummy error code
    push %1         ; vector number
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr_%1
isr_%1:
    push %1         ; vector number (error code already on stack)
    jmp isr_common_stub
%endmacro

; ── Exception vectors 0–31 ──
;
; Error-code vectors per Intel SDM Vol.3 §6.13:
;   #DF(8), #TS(10), #NP(11), #SS(12), #GP(13), #PF(14), #AC(17), #CP(21)

ISR_NOERR 0     ; #DE  Divide Error
ISR_NOERR 1     ; #DB  Debug
ISR_NOERR 2     ; NMI  Non-maskable Interrupt
ISR_NOERR 3     ; #BP  Breakpoint
ISR_NOERR 4     ; #OF  Overflow
ISR_NOERR 5     ; #BR  BOUND Range Exceeded
ISR_NOERR 6     ; #UD  Invalid Opcode
ISR_NOERR 7     ; #NM  Device Not Available
ISR_ERR   8     ; #DF  Double Fault
ISR_NOERR 9     ;      Coprocessor Segment Overrun
ISR_ERR   10    ; #TS  Invalid TSS
ISR_ERR   11    ; #NP  Segment Not Present
ISR_ERR   12    ; #SS  Stack-Segment Fault
ISR_ERR   13    ; #GP  General Protection Fault
ISR_ERR   14    ; #PF  Page Fault
ISR_NOERR 15    ;      Intel reserved
ISR_NOERR 16    ; #MF  x87 FPU Error
ISR_ERR   17    ; #AC  Alignment Check
ISR_NOERR 18    ; #MC  Machine Check
ISR_NOERR 19    ; #XM  SIMD FP Exception
ISR_NOERR 20    ; #VE  Virtualization Exception
ISR_ERR   21    ; #CP  Control Protection Exception

; 22–31: Intel reserved, no error code
%assign i 22
%rep 10
    ISR_NOERR i
    %assign i i+1
%endrep

; ── Common stub: save → call C → restore → iret ──

extern isr_handler

isr_common_stub:
    ; pushad saves regs in order: EAX, ECX, EDX, EBX, old_ESP, EBP, ESI, EDI
    ; EDI is the last pushed → lowest address → first field in registers_t
    pushad

    ; Pass pointer to the register struct as argument to isr_handler.
    ; ESP right now points to the EDI field.
    push esp
    call isr_handler
    add esp, 4

    popad
    add esp, 8          ; pop int_no + err_code
    iret
