section .text
global start
extern kmain
extern _bss_start
extern _bss_end

start:
    cli                         ; disable interrupts (no IDT set up yet)
    cld                         ; clear direction flag (ABI requires DF=0)

    mov esp, stack_top          ; set up kernel stack (grows downward)

    ; Zero the BSS section
    mov edi, _bss_start
.zero_bss:
    cmp edi, _bss_end
    jae .bss_done
    mov dword [edi], 0
    add edi, 4
    jmp .zero_bss
.bss_done:

    ; Pass Multiboot2 info to kmain (x86-32 cdecl: push args right-to-left)
    push ebx                    ; arg2: multiboot_info pointer
    push eax                    ; arg1: magic number (0x36D76289)
    call kmain

    ; kmain should never return; if it does, halt
    cli
    hlt

section .bss
align 16
stack_bottom:
    resb 16384                  ; 16 KB kernel stack
stack_top:
