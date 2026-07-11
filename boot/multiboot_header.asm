section .multiboot
align 8

header_start:
    dd 0xE85250D6              ; magic
    dd 0                        ; architecture: 32-bit i386 protected mode
    dd header_end - header_start ; header_length
    dd -(0xE85250D6 + 0 + (header_end - header_start)) ; checksum

    ; end tag (required)
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
header_end:
