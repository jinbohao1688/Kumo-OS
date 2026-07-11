; user/shell.asm — Kumo Shell v0.1 (flat binary, position-independent)
; Assembled: nasm -f bin -o shell.bin shell.asm
;
; Syscalls used:
;   1  PRINT        — debug print (char value)
;   2  YIELD        — yield CPU
;   7  READCHAR     — non-blocking serial read
;   8  READDIR      — readdir(path, index, name_buf)
;   9  RUN          — run(name) launch registered test
;  10  WRITECONSOLE — raw serial output (buf, len)

bits 32

    ; ── EIP discovery: ebp = code page start ──
    call get_eip
get_eip:
    pop  ebp
    sub  ebp, get_eip

    ; ── Print banner ──
    lea  esi, [ebp + str_banner]
    call puts

    ; ── Main loop ──
main_loop:
    ; Print prompt
    lea  esi, [ebp + str_prompt]
    call puts

    ; Reset line buffer position
    xor  edi, edi              ; edi = buffer index

    ; ── Read line loop ──
read_loop:
    ; Try to read a character
    mov  eax, 7                ; SYSCALL_READCHAR
    int  0x80
    cmp  eax, -1
    jne  .got_char

    ; No input — yield and retry
    mov  eax, 2                ; SYSCALL_YIELD
    int  0x80
    jmp  read_loop

.got_char:
    cmp  eax, 0x0A             ; '\n' — execute command
    je   .execute
    cmp  eax, 0x0D             ; '\r' — ignore (CR from CR+LF terminals)
    je   read_loop
    cmp  eax, 0x08             ; Backspace
    je   .backspace
    cmp  eax, 0x7F             ; Delete
    je   .backspace

    ; Regular character — echo and append to buffer
    cmp  edi, 127              ; buffer full?
    jae  read_loop

    ; Echo: WRITECONSOLE(&c, 1)
    mov  byte [ebp + line_buf + edi], al
    inc  edi
    ; Use stack to pass char pointer
    sub  esp, 4
    mov  [esp], al
    mov  eax, 10               ; SYSCALL_WRITECONSOLE
    mov  ebx, esp              ; buf = pointer to char on stack
    mov  ecx, 1                ; len = 1
    int  0x80
    add  esp, 4
    jmp  read_loop

.backspace:
    test edi, edi
    jz   read_loop             ; buffer empty, ignore
    dec  edi
    ; Erase: WRITECONSOLE("\b \b", 3) — backspace, space, backspace
    sub  esp, 4
    mov  byte [esp], 0x08      ; \b
    mov  byte [esp+1], 0x20    ; space
    mov  byte [esp+2], 0x08    ; \b
    mov  eax, 10
    mov  ebx, esp
    mov  ecx, 3
    int  0x80
    add  esp, 4
    jmp  read_loop

    ; ── Execute command ──
.execute:
    ; Null-terminate the line buffer
    mov  byte [ebp + line_buf + edi], 0

    ; Print newline
    lea  esi, [ebp + str_newline]
    call puts

    ; Empty line? Skip to prompt
    test edi, edi
    jz   main_loop

    ; ── Skip leading spaces ──
    xor  esi, esi              ; esi = offset into line_buf
.skip_spaces:
    mov  al, [ebp + line_buf + esi]
    cmp  al, ' '
    jne  .dispatch
    inc  esi
    cmp  esi, edi
    jb   .skip_spaces
    jmp  main_loop             ; all spaces → empty command

    ; ── Dispatch: compare first word ──
.dispatch:
    ; Check "help"
    lea  ebx, [ebp + line_buf + esi]
    lea  edx, [ebp + cmd_help]
    call strcmp_word
    test eax, eax
    jnz  .try_ls
    call cmd_help_handler
    jmp  main_loop

.try_ls:
    lea  ebx, [ebp + line_buf + esi]
    lea  edx, [ebp + cmd_ls]
    call strcmp_word
    test eax, eax
    jnz  .try_cat
    call cmd_ls_handler
    jmp  main_loop

.try_cat:
    lea  ebx, [ebp + line_buf + esi]
    lea  edx, [ebp + cmd_cat]
    call strcmp_word
    test eax, eax
    jnz  .try_echo
    call cmd_cat_handler
    jmp  main_loop

.try_echo:
    lea  ebx, [ebp + line_buf + esi]
    lea  edx, [ebp + cmd_echo]
    call strcmp_word
    test eax, eax
    jnz  .try_run
    call cmd_echo_handler
    jmp  main_loop

.try_run:
    lea  ebx, [ebp + line_buf + esi]
    lea  edx, [ebp + cmd_run]
    call strcmp_word
    test eax, eax
    jnz  .unknown
    call cmd_run_handler
    jmp  main_loop

.unknown:
    lea  esi, [ebp + str_unknown]
    call puts
    jmp  main_loop


; ═══════════════════════════════════════════════════════════════
; ── puts: print null-terminated string ──
; Input:  esi = absolute address of string
; Output: none (clobbers eax, ebx, ecx)
; ═══════════════════════════════════════════════════════════════
puts:
    push esi
    mov  ecx, 0
.puts_len_loop:
    cmp  byte [esi + ecx], 0
    je   .puts_go
    inc  ecx
    jmp  .puts_len_loop
.puts_go:
    mov  eax, 10               ; SYSCALL_WRITECONSOLE
    mov  ebx, esi              ; buf
    ; ecx = length
    int  0x80
    pop  esi
    ret


; ═══════════════════════════════════════════════════════════════
; ── strcmp_word: compare a word from line buffer against a
;    known command string.  A "word" ends at space or NUL.
; Input:  ebx = line_buf + offset (absolute pointer to command start)
;         edx = command string (null-terminated, absolute)
; Output: eax = 0 if match AND next char is space/NUL,
;              1 if mismatch
; ═══════════════════════════════════════════════════════════════
strcmp_word:
    push esi
    mov  esi, edx             ; esi = command string pointer (avoid dl clobber)
    xor  ecx, ecx
.cmp_loop:
    mov  al, [ebx + ecx]
    mov  dl, [esi + ecx]
    cmp  al, dl
    jne  .no_match
    cmp  dl, 0
    je   .check_end             ; command string ended — check line
    inc  ecx
    jmp  .cmp_loop
.check_end:
    ; Command string matched fully.  Next char in line must be space or NUL.
    mov  al, [ebx + ecx]
    cmp  al, 0
    je   .match
    cmp  al, ' '
    je   .match
.no_match:
    mov  eax, 1
    jmp  .done
.match:
    mov  eax, 0
.done:
    pop  esi
    ret


; ═══════════════════════════════════════════════════════════════
; ── cmd_help_handler ──
; ═══════════════════════════════════════════════════════════════
cmd_help_handler:
    lea  esi, [ebp + str_help]
    call puts
    ret


; ═══════════════════════════════════════════════════════════════
; ── cmd_ls_handler ──
; Uses READDIR to enumerate children of root "/"
; ═══════════════════════════════════════════════════════════════
cmd_ls_handler:
    xor  edi, edi              ; edi = index
.ls_loop:
    mov  eax, 8                ; SYSCALL_READDIR
    lea  ebx, [ebp + str_root] ; path = "/"
    mov  ecx, edi              ; index
    lea  edx, [ebp + name_buf] ; name buffer
    int  0x80
    cmp  eax, 0
    jne  .ls_done              ; -1 = no more entries

    ; Print "  " (indent)
    push edi
    lea  esi, [ebp + str_indent]
    call puts
    ; Print filename
    lea  esi, [ebp + name_buf]
    call puts
    ; Print newline
    lea  esi, [ebp + str_newline]
    call puts
    pop  edi

    inc  edi
    jmp  .ls_loop
.ls_done:
    cmp  edi, 0
    jne  .ls_ret
    lea  esi, [ebp + str_empty]
    call puts
.ls_ret:
    ret


; ═══════════════════════════════════════════════════════════════
; ── cmd_cat_handler ──
; Usage: cat <filename>
; ═══════════════════════════════════════════════════════════════
cmd_cat_handler:
    ; Find argument after "cat "
    ; esi points to start of "cat" in line_buf
    ; Advance past "cat" to find filename
    mov  edi, esi
    ; Skip "cat"
.skip_cat:
    mov  al, [ebp + line_buf + edi]
    cmp  al, 0
    je   .cat_usage
    cmp  al, ' '
    je   .found_space
    inc  edi
    jmp  .skip_cat
.found_space:
    inc  edi                   ; skip space
    ; Skip more spaces
.skip_sp:
    mov  al, [ebp + line_buf + edi]
    cmp  al, ' '
    jne  .cat_arg_start
    inc  edi
    jmp  .skip_sp
.cat_arg_start:
    cmp  al, 0
    je   .cat_usage
    ; edi = offset of filename in line_buf
    ; Build path in name_buf: "/" + filename
    ; For now, just use filename as-is (the path is relative to root)
    ; Actually, just pass the filename pointer directly to open
    lea  ebx, [ebp + line_buf + edi]

    ; Find end of filename (next space or null)
    mov  ecx, edi
.cat_find_end:
    mov  al, [ebp + line_buf + ecx]
    cmp  al, 0
    je   .cat_found_end
    cmp  al, ' '
    je   .cat_found_end
    inc  ecx
    jmp  .cat_find_end
.cat_found_end:
    mov  byte [ebp + line_buf + ecx], 0  ; null-terminate

    ; Build path: "/" + filename
    mov  byte [ebp + name_buf], '/'
    lea  esi, [ebp + line_buf + edi]     ; source (use esi as base)
    lea  edi, [ebp + name_buf + 1]       ; dest (use edi as base)
    xor  ecx, ecx
.cat_cp:
    mov  al, [esi + ecx]
    cmp  al, 0
    je   .cat_cp_done
    mov  [edi + ecx], al
    inc  ecx
    cmp  ecx, 62
    jae  .cat_cp_done
    jmp  .cat_cp
.cat_cp_done:
    mov  byte [edi + ecx], 0

    ; ── open(path, 0) ──
    mov  eax, 3                ; SYSCALL_OPEN
    lea  ebx, [ebp + name_buf]
    mov  ecx, 0                ; O_RDONLY
    int  0x80
    cmp  eax, 0
    jl   .cat_not_found
    mov  [ebp + fd_save], eax

    ; ── Read loop ──
.cat_read_loop:
    mov  eax, 4                ; SYSCALL_READ
    mov  ebx, [ebp + fd_save]
    lea  ecx, [ebp + cat_buf]
    mov  edx, 128              ; read chunk size
    int  0x80
    cmp  eax, 0
    jle  .cat_done

    ; Print the chunk
    push eax                   ; save bytes read
    mov  eax, 10               ; SYSCALL_WRITECONSOLE
    lea  ebx, [ebp + cat_buf]
    pop  ecx                   ; ecx = bytes read
    int  0x80
    jmp  .cat_read_loop

.cat_done:
    ; close
    mov  eax, 6                ; SYSCALL_CLOSE
    mov  ebx, [ebp + fd_save]
    int  0x80
    ret

.cat_not_found:
    lea  esi, [ebp + str_not_found]
    call puts
    ret

.cat_usage:
    lea  esi, [ebp + str_cat_usage]
    call puts
    ret


; ═══════════════════════════════════════════════════════════════
; ── cmd_echo_handler ──
; Usage: echo <text...>
; Prints everything after "echo " to the console
; ═══════════════════════════════════════════════════════════════
cmd_echo_handler:
    ; esi = offset of "echo" in line_buf
    mov  edi, esi
.skip_echo:
    mov  al, [ebp + line_buf + edi]
    cmp  al, 0
    je   .echo_done
    cmp  al, ' '
    je   .echo_arg
    inc  edi
    jmp  .skip_echo
.echo_arg:
    inc  edi                   ; skip space
    ; Skip more spaces
.echo_skip_sp:
    mov  al, [ebp + line_buf + edi]
    cmp  al, ' '
    jne  .echo_print
    inc  edi
    jmp  .echo_skip_sp
.echo_print:
    cmp  al, 0
    je   .echo_done
    ; Print from edi to end of line_buf (null-terminated)
    lea  esi, [ebp + line_buf + edi]
    call puts
.echo_done:
    lea  esi, [ebp + str_newline]
    call puts
    ret


; ═══════════════════════════════════════════════════════════════
; ── cmd_run_handler ──
; Usage: run <name>
; ═══════════════════════════════════════════════════════════════
cmd_run_handler:
    ; esi = offset of "run" in line_buf
    mov  edi, esi
.skip_run:
    mov  al, [ebp + line_buf + edi]
    cmp  al, 0
    je   .run_usage
    cmp  al, ' '
    je   .run_arg
    inc  edi
    jmp  .skip_run
.run_arg:
    inc  edi
.run_skip_sp:
    mov  al, [ebp + line_buf + edi]
    cmp  al, ' '
    jne  .run_do
    inc  edi
    jmp  .run_skip_sp
.run_do:
    cmp  al, 0
    je   .run_usage
    ; Null-terminate the name (find next space or end)
    mov  ecx, edi
.run_find_end:
    mov  al, [ebp + line_buf + ecx]
    cmp  al, 0
    je   .run_found_end
    cmp  al, ' '
    je   .run_found_end
    inc  ecx
    jmp  .run_find_end
.run_found_end:
    mov  byte [ebp + line_buf + ecx], 0

    ; Run it
    mov  eax, 9                ; SYSCALL_RUN
    lea  ebx, [ebp + line_buf + edi]
    int  0x80
    cmp  eax, 0
    jl   .run_fail

    ; Print "started"
    lea  esi, [ebp + str_started]
    call puts
    ret
.run_fail:
    lea  esi, [ebp + str_not_found]
    call puts
    ret
.run_usage:
    lea  esi, [ebp + str_run_usage]
    call puts
    ret


; ═══════════════════════════════════════════════════════════════
; ── Data section ──
; ═══════════════════════════════════════════════════════════════

str_banner:  db "Kumo Shell v0.1", 10, 0
str_prompt:  db "# ", 0
str_newline: db 10, 0
str_indent:  db "  ", 0
str_empty:   db "(empty)", 10, 0
str_root:    db "/", 0
str_not_found: db "not found", 10, 0
str_started: db "started", 10, 0
str_unknown: db "unknown command", 10, 0
str_cat_usage: db "usage: cat <file>", 10, 0
str_run_usage:  db "usage: run <name>", 10, 0

str_help:
    db "Available commands:", 10
    db "  help  - show this help", 10
    db "  ls    - list files", 10
    db "  cat   <file>  - print file content", 10
    db "  echo  <text>   - print text", 10
    db "  run   <test>   - run embedded test", 10, 0

cmd_help:  db "help", 0
cmd_ls:    db "ls", 0
cmd_cat:   db "cat", 0
cmd_echo:  db "echo", 0
cmd_run:   db "run", 0

; ── Buffers ──
line_buf:  times 128 db 0
name_buf:  times 64 db 0
cat_buf:   times 128 db 0
fd_save:   dd 0
