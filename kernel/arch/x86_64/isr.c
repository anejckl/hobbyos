#include "isr.h"
#include "pic.h"
#include "../../drivers/vga.h"
#include "../../debug/debug.h"

static isr_handler_t isr_handlers[256];

static const char *exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

void isr_init(void) {
    for (int i = 0; i < 256; i++)
        isr_handlers[i] = NULL;
}

void isr_register_handler(uint8_t n, isr_handler_t handler) {
    isr_handlers[n] = handler;
}

void isr_unregister_handler(uint8_t n) {
    isr_handlers[n] = NULL;
}

/* Called from isr_stubs.asm common handler */
void isr_handler(struct interrupt_frame *frame) {
    uint64_t int_no = frame->int_no;

    /* Send EOI BEFORE handler dispatch for hardware interrupts.
     * This is critical: if a handler calls schedule() and context_switch(),
     * the EOI would never be sent (until the process is rescheduled),
     * blocking all lower-priority IRQs (e.g., keyboard blocked by PIT). */
    if (int_no >= 32 && int_no < 48) {
        pic_send_eoi((uint8_t)(int_no - 32));
    }

    /* Call registered handler if present */
    if (isr_handlers[int_no]) {
        isr_handlers[int_no](frame);
    } else if (int_no < 32) {
        /* Unhandled CPU exception */
        vga_set_color(VGA_WHITE, VGA_RED);
        vga_printf("\n*** EXCEPTION: %s (INT %u) ***\n",
                   exception_messages[int_no], int_no);
        vga_printf("Error code: 0x%x\n", frame->err_code);
        vga_printf("RIP: 0x%x  RSP: 0x%x\n", frame->rip, frame->rsp);
        vga_printf("RAX: 0x%x  RBX: 0x%x  RCX: 0x%x  RDX: 0x%x\n",
                   frame->rax, frame->rbx, frame->rcx, frame->rdx);

        debug_printf("EXCEPTION: %s (INT %u) err=0x%x RIP=0x%x\n",
                     exception_messages[int_no], int_no,
                     frame->err_code, frame->rip);

        cli();
        for (;;) hlt();
    }
}
