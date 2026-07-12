; switch.asm — cooperative task context switch
;
; void switch_to(task_t *current, task_t *next)
;
; Stack layout on entry (cdecl, caller pushed args right→left):
;   esp[0x00] = ret_addr
;   esp[0x04] = current (arg1)
;   esp[0x08] = next    (arg2)
;
; After push ebp/edi/esi/ebx (saved in reverse pop order):
;   esp[0x00] = ebx          ; pop ebx reads this
;   esp[0x04] = esi          ; pop esi reads this
;   esp[0x08] = edi          ; pop edi reads this
;   esp[0x0C] = ebp          ; pop ebp reads this
;   esp[0x10] = ret_addr
;   esp[0x14] = current
;   esp[0x18] = next

global switch_to

switch_to:
    ; ── Save callee-saved registers (push in REVERSE pop order) ──
    push ebp                    ; esp -= 4, [esp] = ebp
    push edi                    ; esp -= 4, [esp] = edi
    push esi                    ; esp -= 4, [esp] = esi
    push ebx                    ; esp -= 4, [esp] = ebx

    ; ── Save current esp → current->esp (offset 0x00) ──
    mov eax, [esp + 0x14]       ; eax = current (arg1)
    mov [eax], esp              ; current->esp = esp

    ; ── Load next->esp → esp ──
    mov eax, [esp + 0x18]       ; eax = next (arg2)
    mov esp, [eax]              ; esp = next->esp  (offset 0x00)

    ; ── CR3 switch (Phase 12: per-task page directory) ──
    ; Must happen AFTER stack switch (new stack is in shared kernel PTs)
    ; and BEFORE pop (which reads callee-saved regs from the new stack).
    ; eax still holds the 'next' pointer; use it to load next->cr3.
    ; cr3 == 0 means idle or legacy task — skip the switch.
    mov eax, [eax + 0x14]       ; eax = next->cr3
    test eax, eax
    jz .skip_cr3
    mov cr3, eax
.skip_cr3:

    ; ── Restore callee-saved registers (pop in forward order) ──
    pop ebx                     ; esp[0x00] → ebx
    pop esi                     ; esp[0x04] → esi
    pop edi                     ; esp[0x08] → edi
    pop ebp                     ; esp[0x0C] → ebp

    ret                         ; esp[0x10] → eip
