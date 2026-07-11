; user/hello_elf.asm — Minimal ELF test program for Phase 8b
; nasm -f elf32 hello_elf.asm -o hello_elf.o
; i686-elf-ld -T elf_i386.ld hello_elf.o -o hello_elf.elf
;
; Position-independent: uses call/pop EIP discovery.
; Outputs "Hello from ELF!\n" via WRITECONSOLE then yield-loops.

bits 32

global _start

section .text

_start:
    call get_eip
get_eip:
    pop  ebp
    sub  ebp, get_eip

    mov  eax, 10               ; SYSCALL_WRITECONSOLE
    lea  ebx, [ebp + msg]
    mov  ecx, msg_len
    int  0x80

.loop:
    mov  eax, 2                ; SYSCALL_YIELD
    int  0x80
    jmp  .loop

msg:     db "Hello from ELF!", 10
msg_len  equ $ - msg
