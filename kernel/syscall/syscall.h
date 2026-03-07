#ifndef SYSCALL_H
#define SYSCALL_H

#include "../common.h"

/* Syscall numbers */
#define SYS_WRITE       0
#define SYS_EXIT        1
#define SYS_GETPID      2
#define SYS_EXEC        3
#define SYS_WAIT        4
#define SYS_FORK        5
#define SYS_READ        6
#define SYS_OPEN        7
#define SYS_CLOSE       8
#define SYS_PIPE        9
#define SYS_DUP2        10
#define SYS_KILL        11
#define SYS_SIGACTION   12
#define SYS_SIGRETURN   13
#define SYS_GETPPID     14
#define SYS_MKDIR       15
#define SYS_UNLINK      16
#define SYS_GETDENTS    17
#define SYS_STAT        18
#define SYS_SOCKET      19
#define SYS_BIND        20
#define SYS_LISTEN      21
#define SYS_ACCEPT      22
#define SYS_CONNECT     23
#define SYS_SELECT      24

void syscall_init(void);

#endif /* SYSCALL_H */
