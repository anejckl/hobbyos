#ifndef COMMON_H
#define COMMON_H

/* Standard integer types */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;
typedef uint64_t           size_t;
typedef int64_t            ssize_t;
typedef uint64_t           uintptr_t;
typedef int64_t            intptr_t;

#define NULL ((void *)0)
#define true  1
#define false 0
typedef _Bool bool;

/* Memory constants */
#define PAGE_SIZE           4096
#define KERNEL_VMA          0xFFFFFFFF80000000ULL
#define PHYS_MAP_BASE       0xFFFF800000000000ULL

/* Convert between physical and virtual addresses */
#define PHYS_TO_VIRT(addr)  ((void *)((uint64_t)(addr) + PHYS_MAP_BASE))
#define VIRT_TO_PHYS(addr)  ((uint64_t)(addr) - PHYS_MAP_BASE)
#define KERNEL_PHYS_TO_VIRT(addr) ((void *)((uint64_t)(addr) + KERNEL_VMA))
#define KERNEL_VIRT_TO_PHYS(addr) ((uint64_t)(addr) - KERNEL_VMA)

/* Port I/O */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

/* CPU control */
static inline void cli(void) {
    __asm__ volatile ("cli");
}

static inline void sti(void) {
    __asm__ volatile ("sti");
}

static inline void hlt(void) {
    __asm__ volatile ("hlt");
}

static inline uint64_t read_cr2(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}

static inline uint64_t read_cr3(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void write_cr3(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(val) : "memory");
}

static inline void invlpg(uint64_t addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

/* Kernel panic */
void kpanic(const char *msg);

#endif /* COMMON_H */
