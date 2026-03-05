#include "syscall.h"
#include "../arch/x86_64/isr.h"
#include "../drivers/vga.h"
#include "../scheduler/scheduler.h"
#include "../debug/debug.h"

/* Syscall handler for INT 0x80 */
static void syscall_handler(struct interrupt_frame *frame) {
    uint64_t num = frame->rax;
    uint64_t arg1 = frame->rdi;
    uint64_t arg2 = frame->rsi;
    uint64_t arg3 = frame->rdx;

    switch (num) {
    case SYS_WRITE: {
        /* sys_write(fd, buf, len) */
        uint64_t fd = arg1;
        const char *buf = (const char *)arg2;
        uint64_t len = arg3;

        /* Only support stdout (fd=1) */
        if (fd != 1) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* Validate buffer is in user space (below KERNEL_VMA) */
        if ((uint64_t)buf >= KERNEL_VMA || (uint64_t)buf + len >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* Write characters to VGA */
        for (uint64_t i = 0; i < len; i++)
            vga_putchar(buf[i]);

        frame->rax = len;
        return;
    }

    case SYS_EXIT: {
        /* sys_exit(status) */
        struct process *cur = scheduler_get_current();
        debug_printf("syscall: process %u (%s) exit with status %d\n",
                     (uint64_t)cur->pid, cur->name, arg1);
        cur->state = PROCESS_TERMINATED;
        schedule();
        /* Should not return */
        return;
    }

    case SYS_GETPID: {
        struct process *cur = scheduler_get_current();
        frame->rax = (uint64_t)cur->pid;
        return;
    }

    default:
        debug_printf("syscall: unknown syscall %u\n", num);
        frame->rax = (uint64_t)-1;
        return;
    }
}

void syscall_init(void) {
    isr_register_handler(0x80, syscall_handler);
    debug_printf("Syscall handler registered on INT 0x80\n");
}
