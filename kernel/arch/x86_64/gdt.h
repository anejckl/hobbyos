#ifndef GDT_H
#define GDT_H

#include "../../common.h"

/* GDT segment selectors */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_TSS         0x18

/* GDT entry (8 bytes) */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;   /* flags + limit_high */
    uint8_t  base_high;
} __attribute__((packed));

/* TSS descriptor in GDT (16 bytes for 64-bit) */
struct tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

/* GDT pointer */
struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void gdt_init(void);

/* Defined in gdt_flush.asm */
extern void gdt_flush(uint64_t gdt_ptr);
extern void tss_flush(uint16_t selector);

#endif /* GDT_H */
