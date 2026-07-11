; user/test_bad_ptr.asm — ADR-004 bad-pointer interception test
; Passes kernel address 0x100000 to OPEN, expects -1 (rejected by copy_from_user_string).
; Assembled: nasm -f bin -o test_bad_ptr.bin test_bad_ptr.asm

bits 32

    call get_eip
get_eip:
    pop  ebp
    sub  ebp, get_eip

    ; Try to open with a kernel address as path (0x100000 = kernel load address)
    mov  eax, 3                 ; SYSCALL_OPEN
    mov  ebx, 0x100000          ; kernel address — NOT user-accessible
    mov  ecx, 0                 ; O_RDONLY
    int  0x80

    cmp  eax, 0
    jl   .ok                    ; expected: -1 (rejected)

    ; Unexpected: open succeeded — validation did NOT reject the kernel pointer
    mov  eax, 10                ; SYSCALL_WRITECONSOLE
    lea  ebx, [ebp + str_fail]
    mov  ecx, 14
    int  0x80
    jmp  .loop

.ok:
    mov  eax, 10
    lea  ebx, [ebp + str_ok]
    mov  ecx, 12
    int  0x80

.loop:
    mov  eax, 2                 ; SYSCALL_YIELD
    int  0x80
    jmp  .loop

str_ok:   db "BADPTR_OK", 10
str_fail: db "BADPTR_FAIL", 10
