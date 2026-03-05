#ifndef ISR_H
#define ISR_H

#include "../../common.h"

/* Interrupt frame pushed by ISR stubs (must match isr_stubs.asm layout) */
struct interrupt_frame {
    /* Pushed by our stub (reverse order) */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    /* Pushed by our stub */
    uint64_t int_no;
    uint64_t err_code;
    /* Pushed by CPU */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

/* ISR handler function type */
typedef void (*isr_handler_t)(struct interrupt_frame *frame);

/* ISR stub table (defined in isr_stubs.asm) */
extern uint64_t isr_stub_table[256];

/* Register/unregister interrupt handlers */
void isr_init(void);
void isr_register_handler(uint8_t n, isr_handler_t handler);
void isr_unregister_handler(uint8_t n);

/* Called from assembly */
void isr_handler(struct interrupt_frame *frame);

#endif /* ISR_H */
