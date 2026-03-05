#ifndef IDT_H
#define IDT_H

#include "../../common.h"

/* IDT gate types */
#define IDT_GATE_INTERRUPT       0x8E  /* P=1, DPL=0, type=0xE (64-bit interrupt gate) */
#define IDT_GATE_TRAP            0x8F  /* P=1, DPL=0, type=0xF (64-bit trap gate) */
#define IDT_GATE_USER_INTERRUPT  0xEE  /* P=1, DPL=3, type=0xE (user-callable interrupt gate) */

/* IDT entry (16 bytes in 64-bit mode) */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;           /* IST offset (bits 0-2), rest zero */
    uint8_t  type_attr;     /* Gate type + DPL + Present */
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

/* IDT pointer */
struct idt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t type_attr);

/* Defined in idt_flush.asm */
extern void idt_flush(uint64_t idt_ptr);

#endif /* IDT_H */
