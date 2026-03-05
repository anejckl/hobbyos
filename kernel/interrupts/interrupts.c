#include "interrupts.h"
#include "../arch/x86_64/isr.h"
#include "../arch/x86_64/pic.h"
#include "../drivers/vga.h"
#include "../memory/user_vm.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../debug/debug.h"

/* Default exception handlers */
static void breakpoint_handler(struct interrupt_frame *frame) {
    vga_printf("Breakpoint at 0x%x\n", frame->rip);
    debug_printf("Breakpoint at RIP=0x%x\n", frame->rip);
}

static void page_fault_handler(struct interrupt_frame *frame) {
    uint64_t cr2 = read_cr2();
    uint64_t err = frame->err_code;

    /* Check for COW fault: present + write + user (bits 0,1,2 all set) */
    if ((err & 0x7) == 0x7) {
        struct process *cur = scheduler_get_current();
        if (cur && cur->cr3) {
            if (cow_handle_fault(cur->cr3, cr2) == 0)
                return;  /* COW handled successfully */
        }
    }

    /* Fatal page fault */
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_printf("\n*** PAGE FAULT ***\n");
    vga_printf("Address: 0x%x\n", cr2);
    vga_printf("Error code: 0x%x\n", err);
    vga_printf("  %s | %s | %s\n",
               (err & 1) ? "protection" : "not-present",
               (err & 2) ? "write" : "read",
               (err & 4) ? "user" : "kernel");
    vga_printf("RIP: 0x%x\n", frame->rip);
    debug_printf("PAGE FAULT: addr=0x%x err=0x%x RIP=0x%x\n",
                 cr2, err, frame->rip);
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
