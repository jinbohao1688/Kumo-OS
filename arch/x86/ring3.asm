; ring3.asm — enter user mode (Ring3)
;
; void enter_ring3(uint32_t entry, uint32_t user_stack_top)
;
; Sets DS/ES/FS/GS = USER_DS (0x23), builds an iret frame on the
; current (kernel) stack, then executes iret to jump to Ring3.
; This function never returns — execution continues in user mode.
;
; iret pops in this order: EIP, CS, EFLAGS, ESP, SS
; So we push in reverse:  SS, ESP, EFLAGS, CS, EIP
;
; Iret frame layout on kernel stack (high → low addr):
;   [esp+0x10] = SS (0x23)              ← pushed first (highest addr)
;   [esp+0x0C] = ESP (user_stack_top)
;   [esp+0x08] = EFLAGS (IF=1)
;   [esp+0x04] = CS (0x1B)
;   [esp+0x00] = EIP (entry)            ← esp points here before iret

global enter_ring3

enter_ring3:
    ; ── Save args from stack before any modification ──
    mov eax, [esp + 4]        ; eax = entry (arg1)
    mov ecx, [esp + 8]        ; ecx = user_stack_top (arg2)

    ; ── Step 1: Set DS/ES/FS/GS = user data segment 0x23 ──
    ; CPL=0, selector RPL=3: MAX(0,3)=3 ≤ DPL=3 → passes privilege check
    mov dx, 0x23
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx

    ; ── Step 2: Build iret frame (push in reverse pop order) ──
    push dword 0x23           ; esp[+0x10] → SS  (user data selector)
    push ecx                  ; esp[+0x0C] → ESP (user stack top)
    pushfd                    ; esp[+0x08] → EFLAGS (current)
    or dword [esp], 0x200     ; set IF=1 (bit 9) in the pushed EFLAGS
    push dword 0x1B           ; esp[+0x04] → CS  (user code selector)
    push eax                  ; esp[+0x00] → EIP (user entry point)

    ; ── Step 3: Jump to Ring3 ──
    iret
