#include "syscall.h"
#include "../../drivers/serial.h"
#include "../../sched/task.h"
#include "../../fs/vfs.h"
#include "../../mm/kheap.h"
#include "paging.h"

/* run_exec / exec_elf_from_path — defined in kernel/main.c */
extern int run_exec(const char *name);
extern int exec_elf_from_path(const char *path);

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

    case SYSCALL_OPEN: {
        char kpath[256];
        if (copy_from_user_string(kpath, (const char *)a1, sizeof(kpath)) != 0)
            return (uint32_t)-1;
        return (uint32_t)vfs_open(kpath, (int)a2);
    }

    case SYSCALL_READ: {
        uint32_t size = a3;
        if (size == 0) return 0;
        uint8_t *kbuf = (uint8_t *)kmalloc(size);
        if (!kbuf) return (uint32_t)-1;
        int n = vfs_read((int)a1, kbuf, size);
        if (n > 0) {
            if (copy_to_user((void *)a2, kbuf, (uint32_t)n) != 0) {
                kfree(kbuf);
                return (uint32_t)-1;
            }
        }
        kfree(kbuf);
        return (uint32_t)n;
    }

    case SYSCALL_WRITE: {
        uint32_t size = a3;
        if (size == 0) return 0;
        uint8_t *kbuf = (uint8_t *)kmalloc(size);
        if (!kbuf) return (uint32_t)-1;
        if (copy_from_user(kbuf, (const void *)a2, size) != 0) {
            kfree(kbuf);
            return (uint32_t)-1;
        }
        int n = vfs_write((int)a1, kbuf, size);
        kfree(kbuf);
        return (uint32_t)n;
    }

    case SYSCALL_CLOSE:
        return (uint32_t)vfs_close((int)a1);

    case SYSCALL_READCHAR:
        /* Non-blocking serial read.  Shell polls this + yields if no data.
         * READCHAR is currently only used by the shell task — if multiple
         * tasks call it, they will race for serial input bytes. */
        return (uint32_t)serial_poll_char();

    case SYSCALL_READDIR: {
        char kpath[256];
        char kname[64];
        if (copy_from_user_string(kpath, (const char *)a1, sizeof(kpath)) != 0)
            return (uint32_t)-1;
        int ret = vfs_readdir(kpath, a2, kname);
        if (ret != 0)
            return (uint32_t)ret;
        if (copy_to_user((char *)a3, kname, sizeof(kname)) != 0)
            return (uint32_t)-1;
        return 0;
    }

    case SYSCALL_RUN: {
        char kname[256];
        if (copy_from_user_string(kname, (const char *)a1, sizeof(kname)) != 0)
            return (uint32_t)-1;
        return (uint32_t)run_exec(kname);
    }

    case SYSCALL_WRITECONSOLE: {
        uint32_t len = a2;
        if (len == 0) return 0;
        if (len > 4096) return (uint32_t)-1;   /* sanity cap */
        uint8_t *kbuf = (uint8_t *)kmalloc(len);
        if (!kbuf) return (uint32_t)-1;
        if (copy_from_user(kbuf, (const void *)a1, len) != 0) {
            kfree(kbuf);
            return (uint32_t)-1;
        }
        for (uint32_t i = 0; i < len; i++)
            serial_putchar((char)kbuf[i]);
        kfree(kbuf);
        return len;
    }

    case SYSCALL_EXEC: {
        char kpath[256];
        if (copy_from_user_string(kpath, (const char *)a1, sizeof(kpath)) != 0)
            return (uint32_t)-1;
        return (uint32_t)exec_elf_from_path(kpath);
    }

    default:
        serial_write_string("Syscall: unknown number ");
        serial_write_hex(num);
        serial_write_string("\n");
        return 0xFFFFFFFF;   /* -1 = unknown syscall */
    }
}
