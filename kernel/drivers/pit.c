#include "pit.h"
#include "../arch/x86_64/isr.h"
#include "../interrupts/interrupts.h"
#include "../debug/debug.h"

static volatile uint64_t pit_ticks = 0;
static uint32_t pit_frequency = 100;

/* Forward declaration */
void scheduler_tick(void);

static void pit_handler(struct interrupt_frame *frame) {
    (void)frame;
    pit_ticks++;
    scheduler_tick();
}

void pit_init(uint32_t freq) {
    pit_frequency = freq;
    uint32_t divisor = PIT_BASE_FREQ / freq;

    /* Channel 0, lo/hi byte, mode 3 (square wave) */
    outb(PIT_CMD, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register_handler(0, pit_handler);
}

uint64_t pit_get_ticks(void) {
    return pit_ticks;
}

uint64_t pit_get_uptime_seconds(void) {
    return pit_ticks / pit_frequency;
}
