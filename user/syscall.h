#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed int         int32_t;
typedef long long          int64_t;
typedef uint64_t           size_t;

#define NULL ((void *)0)

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
#define SYS_EXECV       26
#define SYS_MMAP        27
#define SYS_MUNMAP      28
#define SYS_MPROTECT    29
#define SYS_BRK         30
#define SYS_EPOLL_CREATE  31
#define SYS_EPOLL_CTL     32
#define SYS_EPOLL_WAIT    33

/* Open flags */
#define O_CREAT         0x40

/* Stat file types */
#define STAT_FILE  1
#define STAT_DIR   2

struct stat_buf {
    uint32_t type;      /* STAT_FILE or STAT_DIR */
    uint32_t size;
    uint32_t inode;
};

/* Signal constants */
#define SIGINT   2
#define SIGFPE   8
#define SIGKILL  9
#define SIGSEGV  11
#define SIGPIPE  13
#define SIGTERM  15
#define SIGCHLD  17
#define SIGTSTP  20

#define SIG_DFL  ((uint64_t)0)
#define SIG_IGN  ((uint64_t)1)

static inline uint64_t syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall2(uint64_t num, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall1(uint64_t num, uint64_t a1) {
    uint64_t ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1)
        : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t sys_write(int fd, const char *buf, uint64_t len) {
    return syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, len);
}

static inline void sys_exit(int status) {
    syscall1(SYS_EXIT, (uint64_t)status);
    for (;;);  /* never returns */
}

static inline uint64_t sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

static inline int64_t sys_exec(const char *path) {
    return (int64_t)syscall1(SYS_EXEC, (uint64_t)path);
}

static inline int64_t sys_wait(int32_t *status) {
    return (int64_t)syscall1(SYS_WAIT, (uint64_t)status);
}

static inline int64_t sys_fork(void) {
    return (int64_t)syscall0(SYS_FORK);
}

static inline int64_t sys_read(int fd, void *buf, uint64_t count) {
    return (int64_t)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, count);
}

static inline int64_t sys_open(const char *path, uint64_t flags) {
    return (int64_t)syscall2(SYS_OPEN, (uint64_t)path, flags);
}

static inline int64_t sys_close(int fd) {
    return (int64_t)syscall1(SYS_CLOSE, (uint64_t)fd);
}

static inline int64_t sys_pipe(int *fds) {
    return (int64_t)syscall1(SYS_PIPE, (uint64_t)fds);
}

static inline int64_t sys_dup2(int oldfd, int newfd) {
    return (int64_t)syscall2(SYS_DUP2, (uint64_t)oldfd, (uint64_t)newfd);
}

static inline int64_t sys_kill(uint32_t pid, int sig) {
    return (int64_t)syscall2(SYS_KILL, (uint64_t)pid, (uint64_t)sig);
}

static inline int64_t sys_sigaction(int sig, uint64_t handler) {
    return (int64_t)syscall2(SYS_SIGACTION, (uint64_t)sig, handler);
}

static inline void sys_sigreturn(void) {
    syscall0(SYS_SIGRETURN);
}

static inline uint64_t sys_getppid(void) {
    return syscall0(SYS_GETPPID);
}

static inline int64_t sys_mkdir(const char *path) {
    return (int64_t)syscall1(SYS_MKDIR, (uint64_t)path);
}

static inline int64_t sys_unlink(const char *path) {
    return (int64_t)syscall1(SYS_UNLINK, (uint64_t)path);
}

static inline int64_t sys_getdents(int fd, void *buf, uint64_t size) {
    return (int64_t)syscall3(SYS_GETDENTS, (uint64_t)fd, (uint64_t)buf, size);
}

static inline int64_t sys_stat(const char *path, struct stat_buf *sb) {
    return (int64_t)syscall2(SYS_STAT, (uint64_t)path, (uint64_t)sb);
}

/* Socket constants */
#define AF_INET     2
#define SOCK_STREAM 2
#define SOCK_DGRAM  1

/* fd_set macros (max 32 FDs) */
typedef uint32_t fd_set_t;
#define FD_ZERO(s)       (*(s) = 0)
#define FD_SET(fd, s)    (*(s) |= (1U << (fd)))
#define FD_CLR(fd, s)    (*(s) &= ~(1U << (fd)))
#define FD_ISSET(fd, s)  (*(s) & (1U << (fd)))

struct select_args {
    uint32_t readfds;
    uint32_t writefds;
    uint32_t exceptfds;
    int32_t timeout_ms;
};

static inline int64_t sys_socket(int domain, int type, int protocol) {
    return (int64_t)syscall3(SYS_SOCKET, (uint64_t)domain, (uint64_t)type, (uint64_t)protocol);
}

static inline int64_t sys_bind(int fd, uint32_t ip, uint16_t port) {
    return (int64_t)syscall3(SYS_BIND, (uint64_t)fd, (uint64_t)ip, (uint64_t)port);
}

static inline int64_t sys_listen(int fd, int backlog) {
    return (int64_t)syscall2(SYS_LISTEN, (uint64_t)fd, (uint64_t)backlog);
}

static inline int64_t sys_accept(int fd) {
    return (int64_t)syscall1(SYS_ACCEPT, (uint64_t)fd);
}

static inline int64_t sys_connect(int fd, uint32_t ip, uint16_t port) {
    return (int64_t)syscall3(SYS_CONNECT, (uint64_t)fd, (uint64_t)ip, (uint64_t)port);
}

static inline int64_t sys_select(int nfds, struct select_args *args) {
    return (int64_t)syscall2(SYS_SELECT, (uint64_t)nfds, (uint64_t)args);
}

static inline int64_t sys_waitpid(int32_t pid, int32_t *status, uint32_t options) {
    return (int64_t)syscall3(SYS_WAITPID, (uint64_t)pid, (uint64_t)status, (uint64_t)options);
}

static inline int64_t sys_execv(const char *path, int64_t argc, char **argv) {
    return (int64_t)syscall3(SYS_EXECV, (uint64_t)path, (uint64_t)argc, (uint64_t)argv);
}

/* User argv address (set by kernel before process starts) */
#define USER_ARGV_ADDR  0x600000ULL

/* mmap prot flags */
#define PROT_NONE      0
#define PROT_READ      1
#define PROT_WRITE     2
#define PROT_EXEC      4

/* mmap flags */
#define MAP_SHARED     1
#define MAP_PRIVATE    2
#define MAP_ANONYMOUS  0x20
#define MAP_FIXED      0x10
#define MAP_FAILED     ((void *)-1)

struct mmap_args {
    uint64_t addr;
    uint64_t len;
    uint32_t prot;
    uint32_t flags;
    int32_t  fd;
    uint32_t pad;
    uint64_t offset;
};

static inline void *sys_mmap(void *addr, uint64_t len, uint32_t prot,
                              uint32_t flags, int32_t fd, uint64_t offset) {
    struct mmap_args args;
    args.addr   = (uint64_t)addr;
    args.len    = len;
    args.prot   = prot;
    args.flags  = flags;
    args.fd     = fd;
    args.pad    = 0;
    args.offset = offset;
    return (void *)syscall1(SYS_MMAP, (uint64_t)&args);
}

static inline int64_t sys_munmap(void *addr, uint64_t len) {
    return (int64_t)syscall2(SYS_MUNMAP, (uint64_t)addr, len);
}

static inline int64_t sys_mprotect(void *addr, uint64_t len, uint32_t prot) {
    return (int64_t)syscall3(SYS_MPROTECT, (uint64_t)addr, len, (uint64_t)prot);
}

static inline uint64_t sys_brk(uint64_t new_brk) {
    return syscall1(SYS_BRK, new_brk);
}

/* epoll constants */
#define EPOLLIN   0x001
#define EPOLLOUT  0x004
#define EPOLLERR  0x008
#define EPOLLHUP  0x010

#define EPOLL_CTL_ADD  1
#define EPOLL_CTL_MOD  2
#define EPOLL_CTL_DEL  3

struct epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));

struct epoll_ctl_args {
    int32_t  op;
    int32_t  fd;
    uint32_t events;
    uint32_t pad;
    uint64_t data;
};

struct epoll_wait_args {
    int32_t  maxevents;
    int32_t  timeout_ms;
    uint64_t events_ptr;
};

static inline int64_t sys_epoll_create(void) {
    return (int64_t)syscall0(SYS_EPOLL_CREATE);
}

static inline int64_t sys_epoll_ctl(int epfd, int op, int fd,
                                     uint32_t events, uint64_t data) {
    struct epoll_ctl_args args;
    args.op     = op;
    args.fd     = fd;
    args.events = events;
    args.pad    = 0;
    args.data   = data;
    return (int64_t)syscall2(SYS_EPOLL_CTL, (uint64_t)epfd, (uint64_t)&args);
}

static inline int64_t sys_epoll_wait(int epfd, struct epoll_event *events,
                                      int maxevents, int timeout_ms) {
    struct epoll_wait_args args;
    args.maxevents  = maxevents;
    args.timeout_ms = timeout_ms;
    args.events_ptr = (uint64_t)events;
    return (int64_t)syscall2(SYS_EPOLL_WAIT, (uint64_t)epfd, (uint64_t)&args);
}

#endif /* USER_SYSCALL_H */
