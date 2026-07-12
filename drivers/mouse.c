#include "mouse.h"
#include "../gfx/primitives.h"
#include "../mm/multiboot.h"
#include "../arch/x86/idt.h"
#include "serial.h"
#include <stdint.h>

/* ── I/O port helpers ── */

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

/* ── PS/2 port numbers ── */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

/* ── PIC data ports ── */
#define PIC2_DATA   0xA1
#define PIC1_DATA   0x21

/* ── Cursor shape ── */
#define CURSOR_W    12
#define CURSOR_H    20

/* ── Cursor state ── */

static int32_t  cursor_x, cursor_y;
static int32_t  cursor_old_x, cursor_old_y;
static int      cursor_visible;

/* Backup buffer: pixel values under the cursor before it was drawn.
 * Always 32-bit regardless of bpp (get_pixel returns uint32_t). */
static uint32_t cursor_backup[CURSOR_W * CURSOR_H];

/* Mouse packet assembly: 3 bytes accumulated across 3 IRQs. */
static uint8_t  mouse_cycle;
static uint8_t  mouse_bytes[3];

/* ── PS/2 controller helpers ── */

static void mouse_wait_write(void)
{
    int timeout = 100000;
    while ((inb(PS2_STATUS) & 0x02) && --timeout) {}
}

static void mouse_wait_read(void)
{
    int timeout = 100000;
    while (!(inb(PS2_STATUS) & 0x01) && --timeout) {}
}

/* ── Cursor save / restore / draw ── */

static void cursor_save(void)
{
    for (int dy = 0; dy < CURSOR_H; dy++)
        for (int dx = 0; dx < CURSOR_W; dx++)
            cursor_backup[dy * CURSOR_W + dx] =
                get_pixel((uint32_t)(cursor_x + dx), (uint32_t)(cursor_y + dy));
}

static void cursor_restore(void)
{
    if (!cursor_visible) return;

    for (int dy = 0; dy < CURSOR_H; dy++)
        for (int dx = 0; dx < CURSOR_W; dx++)
            put_pixel((uint32_t)(cursor_old_x + dx),
                      (uint32_t)(cursor_old_y + dy),
                      cursor_backup[dy * CURSOR_W + dx]);

    cursor_visible = 0;
}

static void cursor_draw(void)
{
    cursor_save();

    uint32_t white = make_color(0xFF, 0xFF, 0xFF);
    fill_rect((uint32_t)cursor_x, (uint32_t)cursor_y,
              CURSOR_W - 1, CURSOR_H - 1, white);

    uint32_t black = make_color(0x00, 0x00, 0x00);
    draw_rect((uint32_t)cursor_x, (uint32_t)cursor_y,
              CURSOR_W - 1, CURSOR_H - 1, black);

    cursor_old_x = cursor_x;
    cursor_old_y = cursor_y;
    cursor_visible = 1;
}

/* ── Packet processing ── */

static uint32_t packet_count = 0;
static int      first_packet = 1;

static void mouse_process_packet(void)
{
    /* Discard the first assembled packet after init.
     * The PS/2 buffer may contain a stray byte (e.g. 0xFA ACK) from
     * the init handshake; if consumed as byte-0 it desyncs every
     * subsequent 3-byte packet boundary. */
    if (first_packet) {
        first_packet = 0;
        return;
    }

    uint8_t flags = mouse_bytes[0];
    int8_t  dx    = (int8_t)mouse_bytes[1];
    int8_t  dy    = (int8_t)mouse_bytes[2];

    /* Diag: first 5 real packets */
    if (++packet_count <= 5) {
        serial_write_string("Mouse: pkt#");
        serial_write_hex(packet_count);
        serial_write_string(" flags=0x");
        serial_write_hex(flags);
        serial_write_string(" dx=");
        serial_write_hex((uint32_t)(int32_t)dx);
        serial_write_string(" dy=");
        serial_write_hex((uint32_t)(int32_t)dy);
        serial_write_string("\n");
    }

    /* ── Button state (print on change) ── */
    static uint8_t last_buttons = 0;
    uint8_t buttons = flags & 0x07;

    if (buttons != last_buttons) {
        serial_write_string("Mouse: buttons=");
        if (buttons & 1) serial_putchar('L');
        if (buttons & 2) serial_putchar('R');
        if (buttons & 4) serial_putchar('M');
        if (buttons == 0) serial_write_string("(none)");
        serial_write_string("\n");
        last_buttons = buttons;
    }

    /* ── Movement ── */
    if (dx != 0 || dy != 0) {
        cursor_restore();

        cursor_x += dx;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_x > (int32_t)g_framebuffer.width - CURSOR_W)
            cursor_x = (int32_t)g_framebuffer.width - CURSOR_W;

        cursor_y -= (int32_t)dy;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_y > (int32_t)g_framebuffer.height - CURSOR_H)
            cursor_y = (int32_t)g_framebuffer.height - CURSOR_H;

        cursor_draw();
    }
}

/* ── Public interface ── */

void mouse_handle_interrupt(void)
{
    uint8_t data = inb(PS2_DATA);

    switch (mouse_cycle) {
    case 0:
        if (!(data & 0x08)) return;
        mouse_bytes[0] = data;
        mouse_cycle = 1;
        break;
    case 1:
        mouse_bytes[1] = data;
        mouse_cycle = 2;
        break;
    case 2:
        mouse_bytes[2] = data;
        mouse_cycle = 0;
        mouse_process_packet();
        break;
    }
}

void mouse_init(void)
{
    serial_write_string("Mouse: initializing PS/2...\n");

    /* ── Step 1: Enable auxiliary (mouse) port ── */
    mouse_wait_write();
    outb(PS2_CMD, 0xA8);
    serial_write_string("Mouse: aux port enabled (0xA8)\n");

    /* ── Step 2: Read + modify + write controller configuration ── */
    mouse_wait_write();
    outb(PS2_CMD, 0x20);
    mouse_wait_read();
    uint8_t cfg = inb(PS2_DATA);

    cfg |=  (1 << 1);               /* enable IRQ12 */
    cfg &= ~(1 << 5);               /* enable mouse clock (0 = on) */

    mouse_wait_write();
    outb(PS2_CMD, 0x60);
    mouse_wait_write();
    outb(PS2_DATA, cfg);
    serial_write_string("Mouse: controller config written (cfg=0x");
    serial_write_hex(cfg);
    serial_write_string(")\n");

    /* ── Step 3: Wire IDT[44] (IRQ12) ── */
    extern void irq12_entry(void);
    idt_set_gate(44, (uint32_t)&irq12_entry, 0x08, 0x8E);
    serial_write_string("Mouse: IDT[44] wired (IRQ12)\n");

    /* ── Step 4: Set Defaults ── */
    mouse_wait_write();
    outb(PS2_CMD, 0xD4);
    mouse_wait_write();
    outb(PS2_DATA, 0xF6);
    mouse_wait_read();
    uint8_t ack1 = inb(PS2_DATA);
    serial_write_string("Mouse: Set Defaults ACK = 0x");
    serial_write_hex(ack1);
    serial_write_string("\n");

    /* ── Step 5: Enable data reporting ── */
    mouse_wait_write();
    outb(PS2_CMD, 0xD4);
    mouse_wait_write();
    outb(PS2_DATA, 0xF4);
    mouse_wait_read();
    uint8_t ack2 = inb(PS2_DATA);
    serial_write_string("Mouse: Enable Reporting ACK = 0x");
    serial_write_hex(ack2);
    serial_write_string("\n");

    /* ── Step 6: Set Stream Mode ── */
    mouse_wait_write();
    outb(PS2_CMD, 0xD4);
    mouse_wait_write();
    outb(PS2_DATA, 0xEA);
    mouse_wait_read();
    uint8_t ack3 = inb(PS2_DATA);
    serial_write_string("Mouse: Stream Mode ACK = 0x");
    serial_write_hex(ack3);
    serial_write_string("\n");

    /* ── Step 7: Unmask IRQ12 (slave PIC bit 4) and IRQ2 (master bit 2) ── */
    uint8_t m1 = inb(PIC1_DATA);
    m1 &= ~(1 << 2);
    outb(PIC1_DATA, m1);

    uint8_t m2 = inb(PIC2_DATA);
    m2 &= ~(1 << 4);
    outb(PIC2_DATA, m2);

    serial_write_string("Mouse: IRQ12 unmasked (PIC mask1=0x");
    serial_write_hex(m1);
    serial_write_string(" mask2=0x");
    serial_write_hex(m2);
    serial_write_string(")\n");

    /* ── Step 8: Init cursor at center of screen ── */
    cursor_x = (int32_t)g_framebuffer.width / 2;
    cursor_y = (int32_t)g_framebuffer.height / 2;
    cursor_old_x = cursor_x;
    cursor_old_y = cursor_y;
    cursor_visible = 0;
    cursor_draw();

    serial_write_string("Mouse: cursor at (");
    serial_write_hex((uint32_t)cursor_x);
    serial_write_string(", ");
    serial_write_hex((uint32_t)cursor_y);
    serial_write_string("), init complete.\n");
}

/* Drain stale bytes from the PS/2 output buffer right before sti. */
void mouse_drain_buf(void)
{
    int drained = 0;
    while ((inb(PS2_STATUS) & 0x01) && drained < 16) {
        (void)inb(PS2_DATA);
        drained++;
    }
}
