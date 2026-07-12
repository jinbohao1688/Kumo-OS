#include "elf.h"
#include "../drivers/serial.h"
#include "../mm/pmm.h"
#include "../mm/kheap.h"
#include "../arch/x86/paging.h"

static const char *phdr_type_name(uint32_t type)
{
    switch (type) {
    case PT_NULL:    return "NULL";
    case PT_LOAD:    return "LOAD";
    case PT_DYNAMIC: return "DYNAMIC";
    case PT_INTERP:  return "INTERP";
    case PT_NOTE:    return "NOTE";
    case PT_PHDR:    return "PHDR";
    default:         return "?";
    }
}

static const char *phdr_flags_str(uint32_t flags)
{
    /* PF_R=4, PF_W=2, PF_X=1 */
    static char buf[4];
    buf[0] = (flags & 4) ? 'R' : '-';
    buf[1] = (flags & 2) ? 'W' : '-';
    buf[2] = (flags & 1) ? 'X' : '-';
    buf[3] = 0;
    return buf;
}

void elf_parse_and_print(const uint8_t *elf_data, uint32_t elf_size)
{
    (void)elf_size;

    serial_write_string("=== ELF Parse (Step 1) ===\n");

    /* ── Validate ELF magic ── */
    if (elf_data[0] != ELFMAG0 || elf_data[1] != ELFMAG1 ||
        elf_data[2] != ELFMAG2 || elf_data[3] != ELFMAG3) {
        serial_write_string("ERROR: not an ELF file (bad magic)\n");
        return;
    }
    if (elf_data[4] != ELFCLASS32) {
        serial_write_string("ERROR: not 32-bit ELF\n");
        return;
    }
    if (elf_data[5] != ELFDATA2LSB) {
        serial_write_string("ERROR: not little-endian ELF\n");
        return;
    }
    serial_write_string("ELF: magic valid, 32-bit LE\n");

    /* ── Parse ELF header ── */
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)elf_data;

    serial_write_string("--- ELF Header ---\n");

    serial_write_string("  e_type     = ");
    serial_write_hex(ehdr->e_type);
    serial_write_string(ehdr->e_type == ET_EXEC ? " (ET_EXEC)" : "");
    serial_write_string("\n");

    serial_write_string("  e_machine  = ");
    serial_write_hex(ehdr->e_machine);
    serial_write_string(ehdr->e_machine == EM_386 ? " (EM_386)" : "");
    serial_write_string("\n");

    serial_write_string("  e_version  = ");
    serial_write_hex(ehdr->e_version);
    serial_write_string("\n");

    serial_write_string("  e_entry    = ");
    serial_write_hex(ehdr->e_entry);
    serial_write_string("\n");

    serial_write_string("  e_phoff    = ");
    serial_write_hex(ehdr->e_phoff);
    serial_write_string("  (program headers at file offset)\n");

    serial_write_string("  e_shoff    = ");
    serial_write_hex(ehdr->e_shoff);
    serial_write_string("  (section headers at file offset)\n");

    serial_write_string("  e_flags    = ");
    serial_write_hex(ehdr->e_flags);
    serial_write_string("\n");

    serial_write_string("  e_ehsize   = ");
    serial_write_hex(ehdr->e_ehsize);
    serial_write_string("\n");

    serial_write_string("  e_phentsize= ");
    serial_write_hex(ehdr->e_phentsize);
    serial_write_string("\n");

    serial_write_string("  e_phnum    = ");
    serial_write_hex(ehdr->e_phnum);
    serial_write_string("\n");

    serial_write_string("  e_shentsize= ");
    serial_write_hex(ehdr->e_shentsize);
    serial_write_string("\n");

    serial_write_string("  e_shnum    = ");
    serial_write_hex(ehdr->e_shnum);
    serial_write_string("\n");

    serial_write_string("  e_shstrndx = ");
    serial_write_hex(ehdr->e_shstrndx);
    serial_write_string("\n");

    /* ── Parse program headers ── */
    serial_write_string("--- Program Headers ---\n");

    if (ehdr->e_phnum == 0) {
        serial_write_string("  (none)\n");
        return;
    }

    if (ehdr->e_phentsize < sizeof(elf32_phdr_t)) {
        serial_write_string("  ERROR: e_phentsize too small\n");
        return;
    }

    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *phdr = (const elf32_phdr_t *)
            (elf_data + ehdr->e_phoff + i * ehdr->e_phentsize);

        serial_write_string("  [");
        serial_write_hex(i);
        serial_write_string("] type=");
        serial_write_string(phdr_type_name(phdr->p_type));
        serial_write_string(" flags=");
        serial_write_string(phdr_flags_str(phdr->p_flags));
        serial_write_string("\n");

        serial_write_string("      offset=");
        serial_write_hex(phdr->p_offset);
        serial_write_string(" vaddr=");
        serial_write_hex(phdr->p_vaddr);
        serial_write_string(" paddr=");
        serial_write_hex(phdr->p_paddr);
        serial_write_string("\n");

        serial_write_string("      filesz=");
        serial_write_hex(phdr->p_filesz);
        serial_write_string(" memsz=");
        serial_write_hex(phdr->p_memsz);
        serial_write_string(" align=");
        serial_write_hex(phdr->p_align);
        serial_write_string("\n");

        if (phdr->p_memsz > phdr->p_filesz)
            serial_write_string("      (BSS: memsz > filesz)\n");
    }

    serial_write_string("=== ELF Parse done ===\n");
}

/* ── Helper: hex dump N bytes at addr ── */
static void hexdump_line(uint32_t addr, const uint8_t *data, uint32_t len)
{
    serial_write_hex(addr);
    serial_write_string(": ");
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        char hi = "0123456789ABCDEF"[b >> 4];
        char lo = "0123456789ABCDEF"[b & 0xF];
        serial_putchar(hi);
        serial_putchar(lo);
        if ((i & 0xF) == 0xF || i == len - 1)
            serial_write_string("\n");
        else
            serial_putchar(' ');
    }
}

/* ── Step 2: load ELF segments into memory ── */
uint32_t elf_load(const uint8_t *elf_data, uint32_t elf_size,
                  uint32_t **out_pages, uint32_t *out_count)
{
    (void)elf_size;

    serial_write_string("\n=== ELF Load (Step 2) ===\n");

    /* Validate magic */
    if (elf_data[0] != ELFMAG0 || elf_data[1] != ELFMAG1 ||
        elf_data[2] != ELFMAG2 || elf_data[3] != ELFMAG3 ||
        elf_data[4] != ELFCLASS32 || elf_data[5] != ELFDATA2LSB) {
        serial_write_string("ERROR: invalid ELF\n");
        return 0;
    }

    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)elf_data;

    /* ── Pass 1: find contiguous virtual address range ── */
    uint32_t min_vaddr = 0xFFFFFFFF;
    uint32_t max_end    = 0;
    int      found_load = 0;

    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *phdr = (const elf32_phdr_t *)
            (elf_data + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD) continue;

        uint32_t seg_start = phdr->p_vaddr;
        uint32_t seg_end   = seg_start + phdr->p_memsz;

        if (seg_start < min_vaddr) min_vaddr = seg_start;
        if (seg_end   > max_end)   max_end   = seg_end;
        found_load = 1;
    }

    if (!found_load) {
        serial_write_string("ERROR: no PT_LOAD segments\n");
        return 0;
    }

    uint32_t total_size = max_end - min_vaddr;
    uint32_t num_pages  = (total_size + 4095) / 4096;

    serial_write_string("ELF: vaddr range [");
    serial_write_hex(min_vaddr);
    serial_write_string(", ");
    serial_write_hex(max_end);
    serial_write_string("), total ");
    serial_write_hex(total_size);
    serial_write_string(" bytes, ");
    serial_write_hex(num_pages);
    serial_write_string(" pages\n");

    /* ── Allocate contiguous pages from USER region ── */
    uint32_t base_phys = pmm_alloc_contiguous_user_pages(num_pages);
    if (base_phys == 0) {
        serial_write_string("ERROR: pmm_alloc_contiguous_user_pages failed\n");
        return 0;
    }

    /* Identity mapping: load_base = base_phys - min_vaddr */
    uint32_t load_base = base_phys - min_vaddr;

    serial_write_string("ELF: base_phys=");
    serial_write_hex(base_phys);
    serial_write_string(" (user region) load_base=");
    serial_write_hex(load_base);
    serial_write_string("\n");

    /* ── Build page list for caller (task_create_user_with_pages) ── */
    *out_count = num_pages;
    *out_pages = (uint32_t *)kmalloc(num_pages * sizeof(uint32_t));
    for (uint32_t i = 0; i < num_pages; i++)
        (*out_pages)[i] = base_phys + i * PAGE_SIZE;

    /* ── Pass 2: copy segment data + zero BSS ── */
    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *phdr = (const elf32_phdr_t *)
            (elf_data + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD) continue;

        uint32_t dest_vaddr = load_base + phdr->p_vaddr;
        /* identity mapping: virtual == physical */
        uint8_t *dest = (uint8_t *)dest_vaddr;
        const uint8_t *src = elf_data + phdr->p_offset;

        serial_write_string("  Load seg vaddr=");
        serial_write_hex(phdr->p_vaddr);
        serial_write_string(" -> dest=");
        serial_write_hex(dest_vaddr);
        serial_write_string(" filesz=");
        serial_write_hex(phdr->p_filesz);

        /* Copy segment data from file */
        for (uint32_t j = 0; j < phdr->p_filesz; j++)
            dest[j] = src[j];

        /* Zero BSS (memsz > filesz) */
        if (phdr->p_memsz > phdr->p_filesz) {
            uint32_t bss_start = phdr->p_filesz;
            uint32_t bss_len   = phdr->p_memsz - phdr->p_filesz;
            serial_write_string(" bss=");
            serial_write_hex(bss_len);
            for (uint32_t j = 0; j < bss_len; j++)
                dest[bss_start + j] = 0;
        }
        serial_write_string("\n");
    }

    /* ── Hex dump: first 0x40 bytes at load_base ── */
    serial_write_string("ELF: first bytes at load_base (");
    serial_write_hex(load_base);
    serial_write_string("):\n");
    hexdump_line(load_base, (const uint8_t *)load_base, 0x37);

    uint32_t entry = load_base + ehdr->e_entry;
    serial_write_string("ELF: entry = load_base + e_entry = ");
    serial_write_hex(load_base);
    serial_write_string(" + ");
    serial_write_hex(ehdr->e_entry);
    serial_write_string(" = ");
    serial_write_hex(entry);
    serial_write_string("\n");
    serial_write_string("=== ELF Load done ===\n");

    return entry;
}

/* ── Step 3: build minimial System V user stack ──
 * Layout (from low address = ESP, upward):
 *   [argc = 1]
 *   [argv[0] = pointer to name string]
 *   [NULL]               ← argv terminator
 *   [NULL]               ← envp terminator (envp has 0 entries)
 *   [padding to 4-byte alignment]
 *   ["<prog_name>\0"]    ← ASCII string, placed first (highest in stack) */
uint32_t elf_setup_user_stack(const char *prog_name, uint32_t *out_page)
{
    /* ── Determine string length ── */
    uint32_t name_len = 0;
    while (prog_name[name_len]) name_len++;
    uint32_t name_bytes = name_len + 1;   /* include NUL terminator */

    /* ── Allocate user stack page from USER region ── */
    uint32_t ustack_page = pmm_alloc_user_page();
    if (ustack_page == 0) {
        serial_write_string("ELF: ERROR pmm_alloc_user_page for user stack\n");
        return 0;
    }
    *out_page = ustack_page;

    uint32_t sp = ustack_page + 4096;   /* top of page, stack grows down */

    /* Phase 1: place string data at high address */
    sp -= name_bytes;
    uint32_t str_addr = sp;             /* argv[0] will point here */
    for (uint32_t i = 0; i < name_bytes; i++)
        ((char *)sp)[i] = prog_name[i];

    /* Phase 2: align sp down to 4-byte boundary for pointer arrays */
    sp = str_addr & ~3u;

    /* Phase 3: build pointer arrays (each step -= 4, going downward) */
    *(uint32_t *)(sp -= 4) = 0;          /* envp[0] = NULL */
    *(uint32_t *)(sp -= 4) = 0;          /* argv terminator NULL */
    *(uint32_t *)(sp -= 4) = str_addr;   /* argv[0] = pointer to string */
    *(uint32_t *)(sp -= 4) = 1;          /* argc = 1 */

    uint32_t final_esp = sp;

    /* ── Serial dump for GDB cross-reference ── */
    serial_write_string("\n=== User Stack Setup (Step 3) ===\n");
    serial_write_string("Stack page : ");
    serial_write_hex(ustack_page);
    serial_write_string(" (user region)\n");
    serial_write_string("Final ESP  : ");
    serial_write_hex(final_esp);
    serial_write_string("\n");
    serial_write_string("argc       : 1\n");
    serial_write_string("prog_name  : \"");
    serial_write_string((char *)prog_name);
    serial_write_string("\" (");
    serial_write_hex(name_len);
    serial_write_string(" chars, at ");
    serial_write_hex(str_addr);
    serial_write_string(")\n");

    serial_write_string("\nStack dump (from ESP upward, 6 dwords):\n");
    for (int i = 0; i < 6; i++) {
        uint32_t addr = final_esp + i * 4;
        serial_write_string("  ESP+");
        serial_write_hex(i * 4);
        serial_write_string(" = ");
        serial_write_hex(addr);
        serial_write_string(" : ");
        serial_write_hex(*(uint32_t *)addr);
        serial_write_string("\n");
    }

    /* Dump the string at str_addr */
    serial_write_string("\nString at argv[0] (");
    serial_write_hex(str_addr);
    serial_write_string("): ");
    serial_write_string((char *)str_addr);
    serial_write_string("\n=== Stack setup done ===\n");

    return final_esp;
}
