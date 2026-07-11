; regtest_a.asm — Task A register preservation test
;
; Loads a distinctive 6-register marker pattern and verifies
; it across cooperative yields and timer-driven preemptions.
; If any register is corrupted, prints an error and halts.
;
; Marker: EBX=0xBBBB  ECX=0xCCCC  EDX=0xDDDD
;          ESI=0xEEEE  EDI=0xFFFF  EBP=0x1111

[bits 32]

section .text
global _start
_start:
    ; ── Initial load ──
    mov ebx, 0xBBBB
    mov ecx, 0xCCCC
    mov edx, 0xDDDD
    mov esi, 0xEEEE
    mov edi, 0xFFFF
    mov ebp, 0x1111

.loop:
    ; ── Register verification block ──
    ; If the timer preempted and corrupted any of these,
    ; the cmp+jnz will catch it.
    cmp ebx, 0xBBBB
    jne .fail
    cmp ecx, 0xCCCC
    jne .fail
    cmp edx, 0xDDDD
    jne .fail
    cmp esi, 0xEEEE
    jne .fail
    cmp edi, 0xFFFF
    jne .fail
    cmp ebp, 0x1111
    jne .fail

    ; ── Yield to other tasks ──
    ; Only EAX is clobbered (syscall num + return value).
    ; pusha/popa in the syscall handler preserve EBX..EBP.
    mov eax, 2          ; SYSCALL_YIELD
    int 0x80

    ; ── Reload EAX as a marker (optional but proves pusha/popa works) ──
    mov eax, 0xAAAA

    jmp .loop

.fail:
    ; Print "FAIL_A" to serial via SYSCALL_WRITECONSOLE
    call .get_msg
    db 'FAIL_A', 0
.get_msg:
    pop ebx             ; ebx = msg ptr (arg1)
    mov ecx, 6          ; len = 6 (arg2)
    mov eax, 10         ; SYSCALL_WRITECONSOLE
    int 0x80

.hang:
    jmp .hang
