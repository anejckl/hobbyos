#include "interrupts.h"
#include "../arch/x86_64/isr.h"
#include "../arch/x86_64/pic.h"
#include "../drivers/vga.h"
#include "../memory/user_vm.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../signal/signal.h"
#include "../debug/debug.h"

/* Kill a user-mode process on fatal fault */
static void kill_user_process(struct process *proc, int sig) {
    debug_printf("fault: killing PID %u with signal %d\n",
                 (uint64_t)proc->pid, (int64_t)sig);

    proc->state = PROCESS_ZOMBIE;
    proc->exit_code = 128 + sig;

    /* Send SIGCHLD to parent */
    signal_send(proc->ppid, SIGCHLD);

    /* Wake parent if waiting */
    struct process *parent = process_get_by_pid(proc->ppid);
    if (parent && parent->state == PROCESS_BLOCKED &&
        (parent->wait_for_pid == 0 || parent->wait_for_pid == proc->pid)) {
        parent->state = PROCESS_READY;
        scheduler_add(parent);
    }

    schedule();
}

/* Default exception handlers */
static void breakpoint_handler(struct interrupt_frame *frame) {
    vga_printf("Breakpoint at 0x%x\n", frame->rip);
    debug_printf("Breakpoint at RIP=0x%x\n", frame->rip);
}

/* Division by zero (INT 0) */
static void div_zero_handler(struct interrupt_frame *frame) {
    uint64_t err = frame->err_code;

    /* User mode fault? */
    struct process *cur = scheduler_get_current();
    if (cur && cur->is_user && (frame->cs & 3)) {
        debug_printf("DIV BY ZERO: PID %u at RIP=0x%x\n",
                     (uint64_t)cur->pid, frame->rip);
        vga_printf("Process %u: division by zero at 0x%x\n",
                   (uint64_t)cur->pid, frame->rip);
        kill_user_process(cur, 8);  /* SIGFPE */
        return;
    }

    /* Kernel fault */
    (void)err;
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_printf("\n*** DIVISION BY ZERO (KERNEL) ***\n");
    vga_printf("RIP: 0x%x\n", frame->rip);
    debug_printf("KERNEL DIV ZERO at RIP=0x%x\n", frame->rip);
    kpanic("division by zero in kernel");
}

/* General Protection Fault (INT 13) */
static void gpf_handler(struct interrupt_frame *frame) {
    uint64_t err = frame->err_code;

    struct process *cur = scheduler_get_current();
    if (cur && cur->is_user && (frame->cs & 3)) {
        debug_printf("GPF: PID %u at RIP=0x%x err=0x%x\n",
                     (uint64_t)cur->pid, frame->rip, err);
        vga_printf("Process %u: general protection fault at 0x%x\n",
                   (uint64_t)cur->pid, frame->rip);
        kill_user_process(cur, 11);  /* SIGSEGV */
        return;
    }

    vga_set_color(VGA_WHITE, VGA_RED);
    vga_printf("\n*** GENERAL PROTECTION FAULT (KERNEL) ***\n");
    vga_printf("Error code: 0x%x\n", err);
    vga_printf("RIP: 0x%x\n", frame->rip);
    debug_printf("KERNEL GPF at RIP=0x%x err=0x%x\n", frame->rip, err);
    kpanic("general protection fault in kernel");
}

static void page_fault_handler(struct interrupt_frame *frame) {
    uint64_t cr2 = read_cr2();
    uint64_t err = frame->err_code;

    /* Check for COW fault: present + write (bits 0,1 set).
     * Can be triggered from user mode (err & 0x4) or from kernel mode
     * when a syscall accesses user-space COW pages (err & 0x4 == 0).
     * Both cases need to be handled if the faulting address is in
     * user space (below KERNEL_VMA). */
    if ((err & 0x3) == 0x3 && cr2 < KERNEL_VMA) {
        struct process *cur = scheduler_get_current();
        if (cur && cur->cr3) {
            if (cow_handle_fault(cur->cr3, cr2) == 0)
                return;  /* COW handled successfully */
        }
    }

    /* User mode page fault — kill the process instead of halting kernel */
    if (err & 0x4) {
        struct process *cur = scheduler_get_current();
        if (cur && cur->is_user) {
            debug_printf("PAGE FAULT (user): PID %u addr=0x%x err=0x%x RIP=0x%x\n",
                         (uint64_t)cur->pid, cr2, err, frame->rip);
            vga_printf("Process %u: segmentation fault at 0x%x\n",
                       (uint64_t)cur->pid, cr2);
            kill_user_process(cur, 11);  /* SIGSEGV */
            return;
        }
    }

    /* Kernel page fault — fatal */
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
    kpanic("page fault in kernel");
}

void interrupts_init(void) {
    isr_init();

    /* Register exception handlers */
    isr_register_handler(0, div_zero_handler);    /* INT 0: div by zero */
    isr_register_handler(3, breakpoint_handler);   /* INT 3: breakpoint */
    isr_register_handler(13, gpf_handler);         /* INT 13: GPF */
    isr_register_handler(14, page_fault_handler);  /* INT 14: page fault */
}

void irq_register_handler(uint8_t irq, isr_handler_t handler) {
    isr_register_handler(irq + PIC1_OFFSET, handler);
    pic_clear_mask(irq);
}
