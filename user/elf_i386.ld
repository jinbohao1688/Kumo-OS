/* user/elf_i386.ld — Linker script for position-independent ELF32
 *
 * Key design: virtual address starts at 0x0.  The ELF loader adds
 * the actual load base at runtime.  All sections are laid out
 * contiguously so the linker can merge them into a single PT_LOAD
 * segment (or at most two: text+rodata merged, data+bss merged).
 *
 * This keeps the loader simple: it only needs to handle PT_LOAD
 * segments, no dynamic linking or relocation. */

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x00000000;

    .text : {
        *(.text)
    }

    . = ALIGN(4096);

    .rodata : {
        *(.rodata)
    }

    . = ALIGN(4096);

    .data : {
        *(.data)
    }

    . = ALIGN(4096);

    .bss : {
        *(.bss)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.eh_frame)
    }
}
