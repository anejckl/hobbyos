#ifndef PIC_H
#define PIC_H

#include "../../common.h"

/* PIC ports */
#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

/* IRQ offsets after remap */
#define PIC1_OFFSET 32
#define PIC2_OFFSET 40

void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);
uint16_t pic_get_isr(void);

#endif /* PIC_H */
