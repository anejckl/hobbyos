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
#define SYS_WAITPID     25
/* SYS_EXECV = 26 (defined in syscall.c as case 26) */
#define SYS_MMAP        27
#define SYS_MUNMAP      28
#define SYS_MPROTECT    29
#define SYS_BRK         30
#define SYS_EPOLL_CREATE  31
#define SYS_EPOLL_CTL     32
#define SYS_EPOLL_WAIT    33
#define SYS_LSEEK       34
#define SYS_RENAME      35
#define SYS_GETTIME     36
#define SYS_IOCTL       37
#define SYS_SEND        38
#define SYS_RECV        39

/* Credential syscalls (Phase 17) */
#define SYS_GETUID      40
#define SYS_GETGID      41
#define SYS_SETUID      42
#define SYS_SETGID      43
#define SYS_CHOWN       44
#define SYS_CHMOD       45
#define SYS_SETEUID     46

/* Networking syscalls (Phase 20) */
#define SYS_FCNTL       47
#define SYS_SENDFILE    48

void syscall_init(void);

#endif /* SYSCALL_H */
