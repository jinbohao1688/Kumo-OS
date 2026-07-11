#include "syscall.h"
#include "../../drivers/serial.h"
#include "../../sched/task.h"
#include "../../fs/vfs.h"

/* run_exec — defined in kernel/main.c (run registry) */
extern int run_exec(const char *name);

uint32_t syscall_dispatch(uint32_t num, uint32_t a1, uint32_t a2,
                          uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a4; (void)a5;   /* unused for now */

    switch (num) {
    case SYSCALL_PRINT:
        serial_write_string("Syscall: Hello from Ring0! (arg=");
        serial_write_hex(a1);
        serial_write_string(")\n");
        return 0;

    case SYSCALL_YIELD:
        task_yield();
        return 0;

    case SYSCALL_OPEN:
        /* FIXME: validate user pointer a1 (path) */
        return (uint32_t)vfs_open((const char *)a1, (int)a2);

    case SYSCALL_READ:
        /* FIXME: validate user buffer a2 */
        return (uint32_t)vfs_read((int)a1, (uint8_t *)a2, a3);

    case SYSCALL_WRITE:
        /* FIXME: validate user buffer a2 */
        return (uint32_t)vfs_write((int)a1, (const uint8_t *)a2, a3);

    case SYSCALL_CLOSE:
        return (uint32_t)vfs_close((int)a1);

    case SYSCALL_READCHAR:
        /* Non-blocking serial read.  Shell polls this + yields if no data.
         * READCHAR is currently only used by the shell task — if multiple
         * tasks call it, they will race for serial input bytes. */
        return (uint32_t)serial_poll_char();

    case SYSCALL_READDIR:
        /* a1 = path ptr, a2 = index, a3 = name_buf ptr */
        /* FIXME: validate user pointer a3 (name_buf) */
        return (uint32_t)vfs_readdir((const char *)a1, a2, (char *)a3);

    case SYSCALL_RUN:
        /* a1 = name ptr — launch a registered embedded test program */
        /* FIXME: validate user pointer a1 */
        return (uint32_t)run_exec((const char *)a1);

    case SYSCALL_WRITECONSOLE:
        /* a1 = buf ptr (user addr), a2 = length */
        /* FIXME: validate user buffer a1 */
        {
            uint32_t n = 0;
            const char *p = (const char *)a1;
            for (uint32_t i = 0; i < a2; i++)
                serial_putchar(p[i]);
            return n;
        }

    default:
        serial_write_string("Syscall: unknown number ");
        serial_write_hex(num);
        serial_write_string("\n");
        return 0xFFFFFFFF;   /* -1 = unknown syscall */
    }
}
