; user/test_boundary.asm — ADR-004 page-boundary string test
; Places a path "/x" at offset 0xFFD (3 bytes before page end).
; With byte-by-byte scan, copy_from_user_string stops at NUL within the same page.
; With old fixed-size 256-byte check, it would validate into the next (non-user) page and fail.
; Assembled: nasm -f bin -o test_boundary.bin test_boundary.asm

bits 32

    call get_eip
get_eip:
    pop  ebp
    sub  ebp, get_eip

    ; ── Step 1: create file "/x" using path1 (not at boundary) ──
    mov  eax, 3                 ; SYSCALL_OPEN
    lea  ebx, [ebp + path1]
    mov  ecx, 1                 ; O_WRONLY
    int  0x80
    mov  [ebp + fd_save], eax

    mov  eax, 6                 ; SYSCALL_CLOSE
    mov  ebx, [ebp + fd_save]
    int  0x80

    ; ── Step 2: open "/x" using path2 (at page boundary, offset 0xFFD) ──
    mov  eax, 3                 ; SYSCALL_OPEN
    lea  ebx, [ebp + path2]
    mov  ecx, 0                 ; O_RDONLY
    int  0x80
    cmp  eax, 0
    jl   .fail                  ; fd < 0 → validation OR file-not-found

    ; Success — close file and report OK
    mov  [ebp + fd_save], eax
    mov  eax, 6                 ; SYSCALL_CLOSE
    mov  ebx, [ebp + fd_save]
    int  0x80

    mov  eax, 10                ; SYSCALL_WRITECONSOLE
    lea  ebx, [ebp + str_ok]
    mov  ecx, 13
    int  0x80
    jmp  .loop

.fail:
    mov  eax, 10
    lea  ebx, [ebp + str_fail]
    mov  ecx, 15
    int  0x80

.loop:
    mov  eax, 2                 ; SYSCALL_YIELD
    int  0x80
    jmp  .loop

; ── Data — placed before boundary padding ──
path1:   db "/x", 0
str_ok:  db "BOUNDARY_OK", 10
str_fail: db "BOUNDARY_FAIL", 10
fd_save: dd 0

; ── Pad with NOPs to push path2 to offset 0xFFD (3 bytes before page end) ──
times (0xFFD - ($ - $$)) db 0x90
path2:   db "/x", 0
