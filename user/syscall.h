#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef long long          int64_t;
typedef uint64_t           size_t;

#define NULL ((void *)0)

/* Syscall numbers */
#define SYS_WRITE   0
#define SYS_EXIT    1
#define SYS_GETPID  2

static inline uint64_t syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
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

#endif /* USER_SYSCALL_H */
