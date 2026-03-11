#include "libc.h"

/* ---- Syscall numbers ---- */
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
#define SYS_MMAP        27
#define SYS_MUNMAP      28
#define SYS_BRK         30
#define SYS_EXECV       26
#define SYS_STAT        18
#define SYS_GETDENTS    17
#define SYS_WAITPID     25
#define SYS_MKDIR       15
#define SYS_UNLINK      16
#define SYS_LSEEK       34
#define SYS_RENAME      35
#define SYS_GETTIME     36
#define SYS_SEND        38
#define SYS_RECV        39

/* ---- Syscall helpers ---- */
static inline uint64_t sc0(uint64_t num) {
    uint64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num) : "rcx", "r11", "memory");
    return ret;
}
static inline uint64_t sc1(uint64_t num, uint64_t a1) {
    uint64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}
static inline uint64_t sc2(uint64_t num, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}
static inline uint64_t sc3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

/* ---- Syscall wrappers ---- */
int64_t sys_write_libc(int fd, const void *buf, size_t len) {
    return (int64_t)sc3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, len);
}
int64_t sys_read_libc(int fd, void *buf, size_t len) {
    return (int64_t)sc3(SYS_READ, (uint64_t)fd, (uint64_t)buf, len);
}
int64_t sys_open_libc(const char *path, uint32_t flags) {
    return (int64_t)sc2(SYS_OPEN, (uint64_t)path, (uint64_t)flags);
}
int64_t sys_close_libc(int fd) {
    return (int64_t)sc1(SYS_CLOSE, (uint64_t)fd);
}
void sys_exit(int status) {
    sc1(SYS_EXIT, (uint64_t)status);
    for (;;);
}
int64_t sys_fork_libc(void) {
    return (int64_t)sc0(SYS_FORK);
}
int64_t sys_wait_libc(int32_t *status) {
    return (int64_t)sc1(SYS_WAIT, (uint64_t)status);
}
int64_t sys_getpid_libc(void) {
    return (int64_t)sc0(SYS_GETPID);
}
uint64_t sys_brk_libc(uint64_t new_brk) {
    return sc1(SYS_BRK, new_brk);
}

struct mmap_args_s {
    uint64_t addr;
    uint64_t len;
    uint32_t prot;
    uint32_t flags;
    int32_t  fd;
    uint32_t pad;
    uint64_t offset;
};

void *sys_mmap_libc(void *addr, size_t len, uint32_t prot,
                     uint32_t flags, int32_t fd, uint64_t offset) {
    struct mmap_args_s args;
    args.addr   = (uint64_t)addr;
    args.len    = len;
    args.prot   = prot;
    args.flags  = flags;
    args.fd     = fd;
    args.pad    = 0;
    args.offset = offset;
    return (void *)sc1(SYS_MMAP, (uint64_t)&args);
}

int64_t sys_munmap_libc(void *addr, size_t len) {
    return (int64_t)sc2(SYS_MUNMAP, (uint64_t)addr, len);
}

int64_t sys_execv_libc(const char *path, int argc, char **argv) {
    return (int64_t)sc3(SYS_EXECV, (uint64_t)path, (uint64_t)argc, (uint64_t)argv);
}

int64_t sys_lseek_libc(int fd, int64_t offset, int whence) {
    return (int64_t)sc3(SYS_LSEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence);
}
int64_t sys_stat_libc(const char *path, struct stat_buf *sb) {
    return (int64_t)sc2(SYS_STAT, (uint64_t)path, (uint64_t)sb);
}
int64_t sys_getdents_libc(int fd, void *buf, uint64_t size) {
    return (int64_t)sc3(SYS_GETDENTS, (uint64_t)fd, (uint64_t)buf, size);
}
int64_t sys_dup2_libc(int oldfd, int newfd) {
    return (int64_t)sc2(SYS_DUP2, (uint64_t)oldfd, (uint64_t)newfd);
}
int64_t sys_pipe_libc(int *fds) {
    return (int64_t)sc1(SYS_PIPE, (uint64_t)fds);
}
int64_t sys_kill_libc(int pid, int sig) {
    return (int64_t)sc2(SYS_KILL, (uint64_t)pid, (uint64_t)sig);
}
int64_t sys_waitpid_libc(int pid, int *status, int options) {
    return (int64_t)sc3(SYS_WAITPID, (uint64_t)pid, (uint64_t)status, (uint64_t)options);
}
int64_t sys_send_libc(int fd, const void *buf, size_t len, int flags) {
    (void)flags;
    return (int64_t)sc3(SYS_SEND, (uint64_t)fd, (uint64_t)buf, len);
}
int64_t sys_recv_libc(int fd, void *buf, size_t len, int flags) {
    (void)flags;
    return (int64_t)sc3(SYS_RECV, (uint64_t)fd, (uint64_t)buf, len);
}
uint64_t sys_gettime_libc(void) {
    return sc0(SYS_GETTIME);
}
int64_t sys_rename_libc(const char *old_path, const char *new_path) {
    return (int64_t)sc2(SYS_RENAME, (uint64_t)old_path, (uint64_t)new_path);
}
int64_t sys_mkdir_libc(const char *path) {
    return (int64_t)sc1(SYS_MKDIR, (uint64_t)path);
}
int64_t sys_unlink_libc(const char *path) {
    return (int64_t)sc1(SYS_UNLINK, (uint64_t)path);
}
