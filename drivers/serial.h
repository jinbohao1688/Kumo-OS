#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_write_string(const char *str);
void serial_write_hex(uint32_t n);

#endif
