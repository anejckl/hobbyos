#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "../common.h"
#include "../arch/x86_64/isr.h"

/* Architecture-independent interrupt interface */
void interrupts_init(void);

/* Register an IRQ handler (IRQ 0-15) */
void irq_register_handler(uint8_t irq, isr_handler_t handler);

#endif /* INTERRUPTS_H */
