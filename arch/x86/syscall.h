#ifndef ARCH_X86_SYSCALL_H
#define ARCH_X86_SYSCALL_H

#include <stdint.h>

/* Syscall numbers */
#define SYSCALL_PRINT  1    /* print a message to serial */

/* C dispatcher — called by syscall_handler (asm stub).
   Returns value to be placed in EAX before returning to user mode. */
uint32_t syscall_dispatch(uint32_t num, uint32_t a1, uint32_t a2,
                          uint32_t a3, uint32_t a4, uint32_t a5);

#endif
