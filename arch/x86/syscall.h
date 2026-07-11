#ifndef ARCH_X86_SYSCALL_H
#define ARCH_X86_SYSCALL_H

#include <stdint.h>

/* Syscall numbers */
#define SYSCALL_PRINT       1   /* print debug msg to serial (arg=value) */
#define SYSCALL_YIELD       2   /* yield CPU */
#define SYSCALL_OPEN        3   /* fd = open(path, flags) */
#define SYSCALL_READ        4   /* n  = read(fd, buf, size) */
#define SYSCALL_WRITE       5   /* n  = write(fd, buf, size) */
#define SYSCALL_CLOSE       6   /* 0  = close(fd) */
#define SYSCALL_READCHAR    7   /* char = readchar() — non-blocking, -1 if none */
#define SYSCALL_READDIR     8   /* 0 = readdir(path, index, name_buf) */
#define SYSCALL_RUN         9   /* task_id = run(name) — launch registered test */
#define SYSCALL_WRITECONSOLE 10 /* n = writeconsole(buf, len) — raw serial output */
#define SYSCALL_EXEC        11 /* task_id = exec(path) — load ELF from VFS and execute */

/* C dispatcher — called by syscall_handler (asm stub).
   Returns value to be placed in EAX before returning to user mode. */
uint32_t syscall_dispatch(uint32_t num, uint32_t a1, uint32_t a2,
                          uint32_t a3, uint32_t a4, uint32_t a5);

#endif
