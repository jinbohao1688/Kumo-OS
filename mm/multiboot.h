#ifndef MM_MULTIBOOT_H
#define MM_MULTIBOOT_H

#include <stdint.h>

/* Multiboot2 magic number — GRUB sets EAX to this before calling kmain. */
#define MULTIBOOT2_MAGIC 0x36D76289

/* ── Tag types (partial) ── */
#define MULTIBOOT_TAG_END         0
#define MULTIBOOT_TAG_MMAP        6   /* memory map */
#define MULTIBOOT_TAG_FRAMEBUFFER 8   /* framebuffer info (response) */

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

/* Framebuffer info tag (type = 8). Layout matches GRUB's multiboot2.h exactly.
 * All field offsets verified against GRUB source — do not reorder. */
typedef struct {
    uint32_t type;                  /* offset 0,  = 8 */
    uint32_t size;                  /* offset 4 */
    uint64_t framebuffer_addr;      /* offset 8 */
    uint32_t framebuffer_pitch;     /* offset 16 */
    uint32_t framebuffer_width;     /* offset 20 */
    uint32_t framebuffer_height;    /* offset 24 */
    uint8_t  framebuffer_bpp;       /* offset 28, 1 byte */
    uint8_t  framebuffer_type;      /* offset 29, 1 byte — 0=indexed, 1=RGB, 2=text */
    uint16_t reserved;              /* offset 30, 2 bytes */
    /* color_info for type=1 starts at offset 32: */
    uint8_t  red_field_position;    /* offset 32 */
    uint8_t  red_mask_size;         /* offset 33 */
    uint8_t  green_field_position;  /* offset 34 */
    uint8_t  green_mask_size;       /* offset 35 */
    uint8_t  blue_field_position;   /* offset 36 */
    uint8_t  blue_mask_size;        /* offset 37 */
} __attribute__((packed)) multiboot_tag_framebuffer_t;

/* ── Kernel-owned framebuffer info (parsed once, no pointers to raw mb_info) ── */

typedef struct {
    uint32_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  type;
    uint8_t  red_pos,  red_mask;
    uint8_t  green_pos, green_mask;
    uint8_t  blue_pos,  blue_mask;
} framebuffer_t;

extern framebuffer_t g_framebuffer;

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
