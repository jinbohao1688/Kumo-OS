; user/test_ramfs.asm — Phase 6 ramfs test (flat binary)
; Assembled: nasm -f bin -o test_ramfs.bin test_ramfs.asm
; Uses call/pop for position-independent addressing.

bits 32

    ; ── EIP discovery: ebp = code page start address ──
    call get_eip
get_eip:
    pop  ebp
    sub  ebp, get_eip

    ; ── 1. fd = open("/test", 1) ──
    mov  eax, 3                 ; SYSCALL_OPEN
    lea  ebx, [ebp + path]
    mov  ecx, 1                 ; O_WRONLY
    int  0x80
    mov  [ebp + fd_save], eax

    ; ── 2. write(fd, "hello", 5) ──
    mov  eax, 5                 ; SYSCALL_WRITE
    mov  ebx, [ebp + fd_save]
    lea  ecx, [ebp + wbuf]
    mov  edx, 5
    int  0x80

    ; ── 3. close(fd) ──
    mov  eax, 6                 ; SYSCALL_CLOSE
    mov  ebx, [ebp + fd_save]
    int  0x80

    ; ── 4. fd = open("/test", 0) ──
    mov  eax, 3                 ; SYSCALL_OPEN
    lea  ebx, [ebp + path]
    mov  ecx, 0                 ; O_RDONLY
    int  0x80
    mov  [ebp + fd_save], eax

    ; ── 5. n = read(fd, rbuf, 5) ──
    mov  eax, 4                 ; SYSCALL_READ
    mov  ebx, [ebp + fd_save]
    lea  ecx, [ebp + rbuf]
    mov  edx, 5
    int  0x80
    mov  [ebp + bytes_rd], eax

    ; ── 6. close(fd) ──
    mov  eax, 6                 ; SYSCALL_CLOSE
    mov  ebx, [ebp + fd_save]
    int  0x80

    ; ── 7. Print verification: each char of rbuf + newline ──
    mov  eax, 1                 ; SYSCALL_PRINT
    movzx ebx, byte [ebp + rbuf + 0]
    int  0x80
    mov  eax, 1
    movzx ebx, byte [ebp + rbuf + 1]
    int  0x80
    mov  eax, 1
    movzx ebx, byte [ebp + rbuf + 2]
    int  0x80
    mov  eax, 1
    movzx ebx, byte [ebp + rbuf + 3]
    int  0x80
    mov  eax, 1
    movzx ebx, byte [ebp + rbuf + 4]
    int  0x80

    ; Print newline + bytes_read count
    mov  eax, 1
    mov  ebx, 0x0A
    int  0x80
    mov  eax, 1
    mov  ebx, [ebp + bytes_rd]
    int  0x80

    ; ── 8. Yield loop ──
.loop:
    mov  eax, 2                 ; SYSCALL_YIELD
    int  0x80
    jmp  .loop

; ── Data (assembled at the end of the binary) ──
path:      db "/test", 0
wbuf:      db "hello"
rbuf:      times 8 db 0
fd_save:   dd 0
bytes_rd:  dd 0
