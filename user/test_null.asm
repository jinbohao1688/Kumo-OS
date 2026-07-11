; user/test_null.asm — ADR-003 NULL page guard test
; After paging_unmap_null_page(), dereferencing address 0 should trigger #PF.
; The exception handler will print register dump then halt.
; If NULL page is still mapped, "NULL_STILL_OK" will appear instead.
; Assembled: nasm -f bin -o test_null.bin test_null.asm

bits 32

    call get_eip
get_eip:
    pop  ebp
    sub  ebp, get_eip

    ; Print start marker
    mov  eax, 10                ; SYSCALL_WRITECONSOLE
    lea  ebx, [ebp + str_start]
    mov  ecx, 10
    int  0x80

    ; Dereference NULL — should trigger #PF and halt via exception handler
    mov  eax, [0]

    ; If we reach here, NULL page is still mapped
    mov  eax, 10
    lea  ebx, [ebp + str_alive]
    mov  ecx, 15
    int  0x80

.loop:
    mov  eax, 2                 ; SYSCALL_YIELD
    int  0x80
    jmp  .loop

str_start: db "NULL_TEST", 10
str_alive: db "NULL_STILL_OK", 10
