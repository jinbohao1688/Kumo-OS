#include "syscall.h"
#include "../../drivers/serial.h"

uint32_t syscall_dispatch(uint32_t num, uint32_t a1, uint32_t a2,
                          uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;   /* unused for now */

    switch (num) {
    case SYSCALL_PRINT:
        serial_write_string("Syscall: Hello from Ring0! (arg=");
        serial_write_hex(a1);
        serial_write_string(")\n");
        return 0;

    default:
        serial_write_string("Syscall: unknown number ");
        serial_write_hex(num);
        serial_write_string("\n");
        return 0xFFFFFFFF;   /* -1 = unknown syscall */
    }
}
