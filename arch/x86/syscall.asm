; syscall.asm — int 0x80 handler stub
;
; On entry (CPU has switched to kernel stack via TSS.ss0:esp0,
; and pushed SS, ESP, EFLAGS, CS, EIP):
;   EAX = syscall number,  EBX/ECX/EDX/ESI/EDI = args 1-5
;   DS/ES/FS/GS = USER_DS (0x23, carried over from ring3)
;
; pusha layout (32-bit, pushes in this register order):
;   [esp+0x00]=edi  [esp+0x04]=esi  [esp+0x08]=ebp  [esp+0x0C]=saved_esp
;   [esp+0x10]=ebx  [esp+0x14]=edx  [esp+0x18]=ecx  [esp+0x1C]=eax

global syscall_handler
extern syscall_dispatch

syscall_handler:
    ; ── Save general-purpose registers ──
    pusha

    ; ── Switch DS/ES/FS/GS → kernel data (0x10) ──
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; ── Call syscall_dispatch(num, a1, a2, a3, a4, a5) ──
    ; Push args right-to-left (cdecl).  Each push shifts esp by -4,
    ; so later pushes must read from adjusted offsets:
    ;   push # | already pushed | reads from orig-offset+N
    ;       1  |      0         | [esp + 0x00] → edi  (arg5)          ; esp+0x00
    ;       2  |      4         | [esp + 0x08] → esi  (arg4)          ; esp+0x04+4
    ;       3  |      8         | [esp + 0x1C] → edx  (arg3)          ; esp+0x14+8
    ;       4  |     12         | [esp + 0x24] → ecx  (arg2)          ; esp+0x18+12
    ;       5  |     16         | [esp + 0x20] → ebx  (arg1)          ; esp+0x10+16
    ;       6  |     20         | [esp + 0x30] → eax  (syscall num)   ; esp+0x1C+20

    push dword [esp + 0x00]     ; arg5 = edi
    push dword [esp + 0x08]     ; arg4 = esi
    push dword [esp + 0x1C]     ; arg3 = edx
    push dword [esp + 0x24]     ; arg2 = ecx
    push dword [esp + 0x20]     ; arg1 = ebx
    push dword [esp + 0x30]     ; syscall number = eax

    call syscall_dispatch
    add esp, 24                 ; clean 6 args

    ; ── Store return value → saved eax slot in pusha frame ──
    ; After add esp,24: pusha data starts at [esp+0x00].
    ; Saved eax is at pusha offset 0x1C.
    mov [esp + 0x1C], eax

    ; ── Restore user data segment BEFORE popa/iret ──
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; ── Restore registers and return to ring3 ──
    popa                        ; restores edi..eax (skips saved_esp)
    iret                        ; pops EIP, CS, EFLAGS, ESP, SS
