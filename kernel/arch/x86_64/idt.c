#include "idt.h"
#include "isr.h"
#include "gdt.h"
#include "../../string.h"

static struct idt_entry idt_entries[256];
static struct idt_pointer idt_ptr;

void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t type_attr) {
    idt_entries[num].offset_low  = handler & 0xFFFF;
    idt_entries[num].offset_mid  = (handler >> 16) & 0xFFFF;
    idt_entries[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt_entries[num].selector    = selector;
    idt_entries[num].ist         = 0;
    idt_entries[num].type_attr   = type_attr;
    idt_entries[num].reserved    = 0;
}

void idt_init(void) {
    memset(idt_entries, 0, sizeof(idt_entries));

    /* Install all 256 ISR stubs */
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, isr_stub_table[i], GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    }

    idt_ptr.limit = sizeof(idt_entries) - 1;
    idt_ptr.base  = (uint64_t)&idt_entries;

    idt_flush((uint64_t)&idt_ptr);
}
