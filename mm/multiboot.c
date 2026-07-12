#include "multiboot.h"
#include "../drivers/serial.h"

memory_map_t g_memory_map;
framebuffer_t g_framebuffer;

void multiboot_parse(unsigned int magic, void *info)
{
    if (magic != MULTIBOOT2_MAGIC) {
        serial_write_string("ERROR: Invalid Multiboot2 magic.\n");
        return;
    }

    multiboot_info_t *mbi = (multiboot_info_t *)info;
    uint8_t *ptr = (uint8_t *)info + sizeof(multiboot_info_t);
    uint8_t *end = (uint8_t *)info + mbi->total_size;

    serial_write_string("Multiboot2 info parsed.\n");

    g_memory_map.count = 0;
    g_memory_map.top_of_memory = 0;

    while (ptr < end) {
        multiboot_tag_t *tag = (multiboot_tag_t *)ptr;

        if (tag->type == MULTIBOOT_TAG_END)
            break;

        if (tag->type == MULTIBOOT_TAG_FRAMEBUFFER) {
            multiboot_tag_framebuffer_t *fb = (multiboot_tag_framebuffer_t *)ptr;
            g_framebuffer.addr   = (uint32_t)fb->framebuffer_addr;
            g_framebuffer.pitch  = fb->framebuffer_pitch;
            g_framebuffer.width  = fb->framebuffer_width;
            g_framebuffer.height = fb->framebuffer_height;
            g_framebuffer.bpp    = fb->framebuffer_bpp;
            g_framebuffer.type   = fb->framebuffer_type;

            serial_write_string("Framebuffer: ");
            serial_write_hex(g_framebuffer.width);
            serial_write_string(" x ");
            serial_write_hex(g_framebuffer.height);
            serial_write_string(" bpp=");
            serial_write_hex(g_framebuffer.bpp);
            serial_write_string(" addr=0x");
            serial_write_hex(g_framebuffer.addr);
            serial_write_string(" type=");
            serial_write_hex(g_framebuffer.type);
            serial_write_string("\n");

            if (fb->framebuffer_type == 1) {
                g_framebuffer.red_pos   = fb->red_field_position;
                g_framebuffer.red_mask  = fb->red_mask_size;
                g_framebuffer.green_pos = fb->green_field_position;
                g_framebuffer.green_mask = fb->green_mask_size;
                g_framebuffer.blue_pos  = fb->blue_field_position;
                g_framebuffer.blue_mask = fb->blue_mask_size;

                serial_write_string("  red:   pos=");
                serial_write_hex(g_framebuffer.red_pos);
                serial_write_string(" mask=");
                serial_write_hex(g_framebuffer.red_mask);
                serial_write_string("\n  green: pos=");
                serial_write_hex(g_framebuffer.green_pos);
                serial_write_string(" mask=");
                serial_write_hex(g_framebuffer.green_mask);
                serial_write_string("\n  blue:  pos=");
                serial_write_hex(g_framebuffer.blue_pos);
                serial_write_string(" mask=");
                serial_write_hex(g_framebuffer.blue_mask);
                serial_write_string("\n");
            } else {
                g_framebuffer.red_pos = g_framebuffer.red_mask = 0;
                g_framebuffer.green_pos = g_framebuffer.green_mask = 0;
                g_framebuffer.blue_pos = g_framebuffer.blue_mask = 0;
                serial_write_string("  (indexed/text mode — no RGB color info)\n");
            }
        }

        if (tag->type == MULTIBOOT_TAG_MMAP) {
            multiboot_tag_mmap_t *mmap_tag = (multiboot_tag_mmap_t *)ptr;
            uint32_t entry_size = mmap_tag->entry_size;
            uint8_t  *entries   = ptr + sizeof(multiboot_tag_mmap_t);
            uint32_t  count     = (mmap_tag->size - sizeof(multiboot_tag_mmap_t))
                                  / entry_size;

            serial_write_string("Memory map:\n");

            for (uint32_t i = 0; i < count; i++) {
                multiboot_mmap_entry_t *e =
                    (multiboot_mmap_entry_t *)(entries + i * entry_size);

                if (e->type == MULTIBOOT_MMAP_AVAILABLE) {
                    serial_write_string("  [AVAILABLE] base=");
                    /* Print high 32 bits if non-zero, then low 32 bits */
                    uint32_t hi = (uint32_t)(e->base_addr >> 32);
                    uint32_t lo = (uint32_t)(e->base_addr);
                    if (hi) {
                        serial_write_hex(hi);
                    }
                    serial_write_hex(lo);
                    serial_write_string("  len=");
                    hi = (uint32_t)(e->length >> 32);
                    lo = (uint32_t)(e->length);
                    if (hi) {
                        serial_write_hex(hi);
                    }
                    serial_write_hex(lo);
                    serial_write_string("\n");

                    /* Store in kernel-owned memory map (32-bit only) */
                    if (e->base_addr < 0x100000000ULL) {
                        uint32_t base   = (uint32_t)e->base_addr;
                        uint32_t length = (uint32_t)e->length;
                        if (e->base_addr + e->length > 0x100000000ULL)
                            length = 0xFFFFFFFF - base + 1;

                        if (g_memory_map.count < MAX_USABLE_REGIONS) {
                            g_memory_map.regions[g_memory_map.count].base   = base;
                            g_memory_map.regions[g_memory_map.count].length = length;
                            g_memory_map.count++;

                            uint32_t region_top = base + length;
                            if (region_top > g_memory_map.top_of_memory)
                                g_memory_map.top_of_memory = region_top;
                        }
                    }
                }
            }
        }

        /* Advance to next tag. Tags are 8-byte aligned per the spec. */
        uint32_t tag_size = tag->size;
        if (tag_size < 8)
            break;   /* malformed tag */
        tag_size = (tag_size + 7) & ~7;   /* round up to 8 */
        ptr += tag_size;
    }
}
