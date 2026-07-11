#ifndef FS_ELF_H
#define FS_ELF_H

#include <stdint.h>

/* ── ELF32 header ── */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf32_ehdr_t;

/* ── ELF32 program header ── */
typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} elf32_phdr_t;

/* ── ELF identification ── */
#define ELFMAG0  0x7F
#define ELFMAG1  'E'
#define ELFMAG2  'L'
#define ELFMAG3  'F'

#define ELFCLASS32  1
#define ELFDATA2LSB 1

#define ET_EXEC  2
#define EM_386   3

/* ── Program header types ── */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_PHDR    6

/* ── Step 1: parse and print ELF header + program headers ── */
void elf_parse_and_print(const uint8_t *elf_data, uint32_t elf_size);

/* ── Step 2: load ELF segments into memory ──
 * Allocates contiguous physical pages for all PT_LOAD segments,
 * sets user-accessible, copies segment data from file, zeros BSS.
 * Returns the absolute entry point (load_base + e_entry), or 0 on failure. */
uint32_t elf_load(const uint8_t *elf_data, uint32_t elf_size);

/* ── Step 3: build user stack with argc/argv ──
 * Allocates and populates a user stack page with:
 *   [argc=1] [argv[0]=ptr_to_name] [NULL] [NULL] [name_string]
 * Returns the initial ESP value for the task, or 0 on failure. */
uint32_t elf_setup_user_stack(const char *prog_name);

#endif
