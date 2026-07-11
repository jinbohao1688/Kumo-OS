#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_write_string(const char *str);
void serial_write_hex(uint32_t n);
void serial_putchar(char c);
int  serial_poll_char(void);    /* -1 if no data, else character */

#endif
