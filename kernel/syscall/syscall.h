#ifndef SYSCALL_H
#define SYSCALL_H

#include "../common.h"

/* Syscall numbers */
#define SYS_WRITE   0
#define SYS_EXIT    1
#define SYS_GETPID  2
#define SYS_EXEC    3
#define SYS_WAIT    4
#define SYS_FORK    5

void syscall_init(void);

#endif /* SYSCALL_H */
