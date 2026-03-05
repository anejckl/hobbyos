#ifndef SYSCALL_H
#define SYSCALL_H

#include "../common.h"

/* Syscall numbers */
#define SYS_WRITE   0
#define SYS_EXIT    1
#define SYS_GETPID  2

void syscall_init(void);

#endif /* SYSCALL_H */
