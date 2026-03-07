/*
 * stubs.h — Host-side stubs for kernel-only constructs.
 *
 * When compiling kernel .c files with the host gcc for unit testing,
 * this header replaces kernel/common.h and provides no-op implementations
 * of hardware instructions, address macros, and panic handling.
 */

#ifndef TEST_STUBS_H
#define TEST_STUBS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- Null ---- */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* ---- Memory constants ---- */
#define PAGE_SIZE           4096
#define KERNEL_VMA          0xFFFFFFFF80000000ULL
#define PHYS_MAP_BASE       0xFFFF800000000000ULL

/* ---- Address conversion: identity on the host ---- */
#define PHYS_TO_VIRT(addr)          ((void *)((uintptr_t)(addr)))
#define VIRT_TO_PHYS(addr)          ((uintptr_t)(addr))
#define KERNEL_PHYS_TO_VIRT(addr)   ((void *)((uintptr_t)(addr)))
#define KERNEL_VIRT_TO_PHYS(addr)   ((uintptr_t)(addr))

/* ---- Port I/O stubs (no-ops) ---- */
static inline void outb(uint16_t port, uint8_t val) {
    (void)port; (void)val;
}
static inline uint8_t inb(uint16_t port) {
    (void)port; return 0;
}
static inline void outw(uint16_t port, uint16_t val) {
    (void)port; (void)val;
}
static inline uint16_t inw(uint16_t port) {
    (void)port; return 0;
}
static inline void outl(uint16_t port, uint32_t val) {
    (void)port; (void)val;
}
static inline uint32_t inl(uint16_t port) {
    (void)port; return 0;
}
static inline void io_wait(void) {}

/* ---- CPU control stubs (no-ops) ---- */
static inline void cli(void) {}
static inline void sti(void) {}
static inline void hlt(void) {}

static inline uint64_t read_cr2(void) { return 0; }
static inline uint64_t read_cr3(void) { return 0; }
static inline void write_cr3(uint64_t val) { (void)val; }
static inline void invlpg(uint64_t addr) { (void)addr; }

/* ---- Kernel panic: abort on host ---- */
static inline void kpanic(const char *msg) {
    fprintf(stderr, "KERNEL PANIC (test): %s\n", msg);
    abort();
}

/* ---- Mock VGA buffer ---- */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  0  /* unused in tests */

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE = 1, VGA_GREEN = 2, VGA_CYAN = 3,
    VGA_RED = 4, VGA_MAGENTA = 5, VGA_BROWN = 6, VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8, VGA_LIGHT_BLUE = 9, VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN = 11, VGA_LIGHT_RED = 12, VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW = 14, VGA_WHITE = 15,
};

/* ---- COM1 stub ---- */
#define COM1_PORT 0x3F8

#endif /* TEST_STUBS_H */
