#include "interrupts.h"
#include "../arch/x86_64/isr.h"
#include "../arch/x86_64/pic.h"
#include "../drivers/vga.h"
#include "../debug/debug.h"

/* Default exception handlers */
static void breakpoint_handler(struct interrupt_frame *frame) {
    vga_printf("Breakpoint at 0x%x\n", frame->rip);
    debug_printf("Breakpoint at RIP=0x%x\n", frame->rip);
}

static void page_fault_handler(struct interrupt_frame *frame) {
    uint64_t cr2 = read_cr2();
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_printf("\n*** PAGE FAULT ***\n");
    vga_printf("Address: 0x%x\n", cr2);
    vga_printf("Error code: 0x%x\n", frame->err_code);
    vga_printf("  %s | %s | %s\n",
               (frame->err_code & 1) ? "protection" : "not-present",
               (frame->err_code & 2) ? "write" : "read",
               (frame->err_code & 4) ? "user" : "kernel");
    vga_printf("RIP: 0x%x\n", frame->rip);
    debug_printf("PAGE FAULT: addr=0x%x err=0x%x RIP=0x%x\n",
                 cr2, frame->err_code, frame->rip);
    cli();
    for (;;) hlt();
}

void interrupts_init(void) {
    isr_init();

    /* Register default exception handlers */
    isr_register_handler(3, breakpoint_handler);
    isr_register_handler(14, page_fault_handler);
}

void irq_register_handler(uint8_t irq, isr_handler_t handler) {
    isr_register_handler(irq + PIC1_OFFSET, handler);
    pic_clear_mask(irq);
}
