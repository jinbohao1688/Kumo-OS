; regtest_b.asm — Task B register preservation test
;
; Loads a distinctive 6-register marker pattern (different from
; Task A so cross-contamination is detectable) and verifies it
; across cooperative yields and timer-driven preemptions.
;
; Marker: EBX=0x2222  ECX=0x3333  EDX=0x4444
;          ESI=0x5555  EDI=0x6666  EBP=0x7777

[bits 32]

section .text
global _start
_start:
    mov ebx, 0x2222
    mov ecx, 0x3333
    mov edx, 0x4444
    mov esi, 0x5555
    mov edi, 0x6666
    mov ebp, 0x7777

.loop:
    cmp ebx, 0x2222
    jne .fail
    cmp ecx, 0x3333
    jne .fail
    cmp edx, 0x4444
    jne .fail
    cmp esi, 0x5555
    jne .fail
    cmp edi, 0x6666
    jne .fail
    cmp ebp, 0x7777
    jne .fail

    mov eax, 2          ; SYSCALL_YIELD
    int 0x80

    mov eax, 0xBBBB

    jmp .loop

.fail:
    call .get_msg
    db 'FAIL_B', 0
.get_msg:
    pop ebx
    mov ecx, 6
    mov eax, 10         ; SYSCALL_WRITECONSOLE
    int 0x80

.hang:
    jmp .hang
