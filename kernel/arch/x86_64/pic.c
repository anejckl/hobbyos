#include "pic.h"

/* ICW1 */
#define ICW1_ICW4       0x01
#define ICW1_INIT       0x10

/* ICW4 */
#define ICW4_8086       0x01

void pic_init(void) {
    /* Save masks */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);
    (void)mask1;
    (void)mask2;

    /* ICW1: Start initialization sequence */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* ICW2: Set vector offsets */
    outb(PIC1_DATA, PIC1_OFFSET);  /* IRQ 0-7  → INT 32-39 */
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);  /* IRQ 8-15 → INT 40-47 */
    io_wait();

    /* ICW3: Cascade configuration */
    outb(PIC1_DATA, 0x04);         /* IRQ2 has slave */
    io_wait();
    outb(PIC2_DATA, 0x02);         /* Slave identity = 2 */
    io_wait();

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Mask all IRQs initially */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

void pic_set_mask(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t val = inb(port) | (1 << irq);
    outb(port, val);
}

void pic_clear_mask(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t val = inb(port) & ~(1 << irq);
    outb(port, val);
}

uint16_t pic_get_isr(void) {
    outb(PIC1_CMD, 0x0B);
    outb(PIC2_CMD, 0x0B);
    return (inb(PIC2_CMD) << 8) | inb(PIC1_CMD);
}
