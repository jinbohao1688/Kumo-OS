#ifndef MM_MULTIBOOT_H
#define MM_MULTIBOOT_H

#include <stdint.h>

/* Multiboot2 magic number — GRUB sets EAX to this before calling kmain. */
#define MULTIBOOT2_MAGIC 0x36D76289

/* ── Tag types (partial) ── */
#define MULTIBOOT_TAG_END  0
#define MULTIBOOT_TAG_MMAP 6   /* memory map */

/* ── Memory map entry flags ── */
#define MULTIBOOT_MMAP_AVAILABLE 1

/* Generic tag header (8 bytes). Every tag starts with these two fields. */
typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) multiboot_tag_t;

/* Multiboot2 info header (8 bytes). Followed by tags. */
typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed)) multiboot_info_t;

/* Memory map tag (type = 6). */
typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    /* Followed by entries[] — each entry is multiboot_mmap_entry_t */
} __attribute__((packed)) multiboot_tag_mmap_t;

/* A single memory map entry (24 bytes). */
typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed)) multiboot_mmap_entry_t;

/* ── Kernel-owned memory map (extracted once, no pointers to raw mb_info) ── */

#define MAX_USABLE_REGIONS 16

typedef struct {
    uint32_t base;
    uint32_t length;
} usable_region_t;

typedef struct {
    usable_region_t regions[MAX_USABLE_REGIONS];
    uint32_t count;
    uint32_t top_of_memory;   /* highest usable address (end of highest region) */
} memory_map_t;

extern memory_map_t g_memory_map;

void multiboot_parse(unsigned int magic, void *info);

#endif
