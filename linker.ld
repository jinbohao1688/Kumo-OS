/* Linker script for Kumo OS — 32-bit protected mode, lower-half kernel */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(start)

SECTIONS
{
    /* Load at 1MB: above all BIOS/VGA/firmware reserved regions */
    . = 1M;

    /* Multiboot2 header must be within the first 32768 bytes of the image.
       Placing it first guarantees this without extra alignment games. */
    .multiboot : {
        *(.multiboot)
    }

    . = ALIGN(4K);
    _text_start = .;
    .text : {
        *(.text)
    }
    _text_end = .;

    . = ALIGN(4K);
    _rodata_start = .;
    .rodata : {
        *(.rodata)
    }
    _rodata_end = .;

    . = ALIGN(4K);
    _data_start = .;
    .data : {
        *(.data)
    }
    _data_end = .;

    . = ALIGN(4K);
    _bss_start = .;
    .bss : {
        *(COMMON)
        *(.bss)
    }
    _bss_end = .;

    _kernel_end = .;
}
