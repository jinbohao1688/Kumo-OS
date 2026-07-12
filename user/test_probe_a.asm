; user/test_probe_a.asm — Phase 12 isolation verification (probe A)
; Self-reads magic 0xAA at offset 0x200, then attempts cross-task read
; of probe_b's magic at 0x0080E200 — expected to trigger #PF (error 0x05).
; Assembled: nasm -f bin -o test_probe_a.bin test_probe_a.asm

bits 32

    call get_eip
get_eip:
    pop  ebp
    sub  ebp, get_eip

    ; ── Self-test: read own magic at offset 0x200 ──
    mov  eax, [ebp + 0x200]
    cmp  eax, 0xAA
    jne  .self_fail

    ; Print "A: self OK"
    mov  eax, 10               ; SYSCALL_WRITECONSOLE
    lea  ebx, [ebp + str_self_ok]
    mov  ecx, str_self_ok_len
    int  0x80

    ; ── Cross-test: read probe_b's magic (expected #PF) ──
    ; probe_b code page = 0x0080E000 (deterministic PMM order).
    ; magic_b is at offset 0x200 within that page.
    ; This PTE is supervisor-only in probe_a's private PD →
    ; #PF with error code 0x05 (user read, protection violation).
    mov  eax, [0x0080E000 + 0x200]

    ; If we reach here, ISOLATION FAILED
    mov  eax, 10
    lea  ebx, [ebp + str_isol_fail]
    mov  ecx, str_isol_fail_len
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
str_self_ok:   db "A: self OK", 10
str_self_ok_len equ $ - str_self_ok
str_self_fail:  db "A: self FAIL", 10
str_self_fail_len equ $ - str_self_fail
str_isol_fail:  db "A: ISOLATION FAILED", 10
str_isol_fail_len equ $ - str_isol_fail

; ── Pad magic_a to offset 0x200 ──
times (0x200 - ($ - $$)) db 0
magic_a: dd 0xAA
