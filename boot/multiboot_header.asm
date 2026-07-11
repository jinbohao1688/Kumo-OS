section .multiboot
align 8

header_start:
    dd 0xE85250D6              ; magic
    dd 0                        ; architecture: 32-bit i386 protected mode
    dd header_end - header_start ; header_length
    dd -(0xE85250D6 + 0 + (header_end - header_start)) ; checksum

    ; Framebuffer request tag (type=5) — ask GRUB to set up a linear framebuffer.
    align 8
    dw 5                        ; type
    dw 0                        ; flags (0 = let GRUB pick best mode)
    dd 20                       ; size
    dd 0                        ; width  (0 = don't care)
    dd 0                        ; height (0 = don't care)
    dd 0                        ; depth  (0 = don't care)
    align 8                     ; pad to 8-byte boundary

    ; end tag (required)
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
header_end:
