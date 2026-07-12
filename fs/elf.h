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
 * Allocates contiguous physical pages from the USER region for all PT_LOAD
 * segments, copies segment data from file, zeros BSS.  Does NOT mark pages
 * user-accessible — that responsibility moved to task_create_user_with_pages.
 *
 * Returns the absolute entry point (load_base + e_entry), or 0 on failure.
 * On success, *out_pages is a kmalloc'd array of physical page addresses
 * (caller must kfree), and *out_count is the number of pages. */
uint32_t elf_load(const uint8_t *elf_data, uint32_t elf_size,
                  uint32_t **out_pages, uint32_t *out_count);

/* ── Step 3: build user stack with argc/argv ──
 * Allocates a user-region page and populates it with:
 *   [argc=1] [argv[0]=ptr_to_name] [NULL] [NULL] [name_string]
 * Does NOT mark the page user-accessible (see elf_load note).
 *
 * Returns the initial ESP value for the task, or 0 on failure.
 * On success, *out_page receives the physical address of the stack page. */
uint32_t elf_setup_user_stack(const char *prog_name, uint32_t *out_page);

#endif
