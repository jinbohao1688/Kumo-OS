/* COM1 serial port driver — polling (no interrupts, no flow control) */

#include <stdint.h>

#define COM1_PORT 0x3F8

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

void serial_init(void)
{
    /* Disable all interrupts */
    outb(COM1_PORT + 1, 0x00);

    /* Set DLAB to configure baud rate divisor */
    outb(COM1_PORT + 3, 0x80);

    /* Divisor = 3 (lo, hi) => 115200 / 3 = 38400 baud */
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);

    /* 8 data bits, 1 stop bit, no parity, clear DLAB */
    outb(COM1_PORT + 3, 0x03);

    /* Enable FIFO, clear TX+RX queues, 14-byte trigger level */
    outb(COM1_PORT + 2, 0xC7);

    /* DTR + RTS + OUT2 (OUT2 required for QEMU) */
    outb(COM1_PORT + 4, 0x0B);
}

static int serial_is_tx_empty(void)
{
    /* Line Status Register bit 5: Transmitter Holding Register Empty */
    return inb(COM1_PORT + 5) & 0x20;
}

static void serial_write_char(char c)
{
    /* Convert LF to CR+LF for proper terminal output */
    if (c == '\n') {
        while (!serial_is_tx_empty())
            ;
        outb(COM1_PORT, '\r');
    }
    while (!serial_is_tx_empty())
        ;
    outb(COM1_PORT, c);
}

void serial_write_string(const char *str)
{
    while (*str) {
        serial_write_char(*str++);
    }
}

void serial_write_hex(uint32_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    serial_write_string("0x");
    for (int i = 7; i >= 0; i--) {
        serial_write_char(hex[(n >> (i * 4)) & 0xF]);
    }
}
