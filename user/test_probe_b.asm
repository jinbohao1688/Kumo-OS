; user/test_probe_b.asm — Phase 12 isolation verification (probe B)
; Self-reads magic 0xBB at offset 0x200, prints result, then loops.
; probe_a will later attempt to read this page → expected #PF.
; Assembled: nasm -f bin -o test_probe_b.bin test_probe_b.asm

bits 32

    call get_eip
get_eip:
    pop  ebp
    sub  ebp, get_eip

    ; ── Self-test: read own magic at offset 0x200 ──
    mov  eax, [ebp + 0x200]
    cmp  eax, 0xBB
    jne  .self_fail

    ; Print "B: self OK"
    mov  eax, 10               ; SYSCALL_WRITECONSOLE
    lea  ebx, [ebp + str_self_ok]
    mov  ecx, str_self_ok_len
    int  0x80
    jmp  .loop

.self_fail:
    mov  eax, 10
    lea  ebx, [ebp + str_self_fail]
    mov  ecx, str_self_fail_len
    int  0x80

.loop:
    mov  eax, 2                ; SYSCALL_YIELD
    int  0x80
    jmp  .loop

; ── Data ──
str_self_ok:   db "B: self OK", 10
str_self_ok_len equ $ - str_self_ok
str_self_fail:  db "B: self FAIL", 10
str_self_fail_len equ $ - str_self_fail

; ── Pad magic_b to offset 0x200 ──
times (0x200 - ($ - $$)) db 0
magic_b: dd 0xBB
