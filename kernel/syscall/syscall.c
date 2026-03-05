#include "syscall.h"
#include "../arch/x86_64/isr.h"
#include "../arch/x86_64/fork_return.h"
#include "../drivers/vga.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../process/user_process.h"
#include "../memory/user_vm.h"
#include "../memory/kheap.h"
#include "../fs/ramfs.h"
#include "../string.h"
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

        /* Mirror to serial for automated testing */
        for (uint64_t i = 0; i < len; i++)
            debug_putchar(buf[i]);

        frame->rax = len;
        return;
    }

    case SYS_EXIT: {
        /* sys_exit(status) */
        struct process *cur = scheduler_get_current();
        struct process *table = process_table_get();
        debug_printf("syscall: process %u (%s) exit with status %d\n",
                     (uint64_t)cur->pid, cur->name, arg1);
        cur->state = PROCESS_ZOMBIE;
        cur->exit_code = (int32_t)arg1;

        /* Reparent children to PID 1 */
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (table[i].ppid == cur->pid &&
                table[i].state != PROCESS_UNUSED &&
                table[i].state != PROCESS_TERMINATED) {
                table[i].ppid = 1;
            }
        }

        /* Wake blocked parent */
        struct process *parent = process_get_by_pid(cur->ppid);
        if (parent && parent->state == PROCESS_BLOCKED &&
            (parent->wait_for_pid == 0 || parent->wait_for_pid == cur->pid)) {
            parent->state = PROCESS_READY;
            scheduler_add(parent);
        }

        schedule();
        /* Should not return */
        return;
    }

    case SYS_GETPID: {
        struct process *cur = scheduler_get_current();
        frame->rax = (uint64_t)cur->pid;
        return;
    }

    case SYS_EXEC: {
        /* sys_exec(path) — path is a user-space string */
        const char *path = (const char *)arg1;
        struct process *cur = scheduler_get_current();

        /* Validate pointer is in user space */
        if ((uint64_t)path >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* Look up program in RAMFS */
        uint64_t prog_size = 0;
        const uint8_t *prog_data = ramfs_get_file_data(path, &prog_size);
        if (!prog_data) {
            debug_printf("syscall: exec '%s' not found\n", path);
            frame->rax = (uint64_t)-1;
            return;
        }

        /* Create new process from ELF data */
        if (user_process_create(path, prog_data, prog_size, cur->pid) < 0) {
            frame->rax = (uint64_t)-1;
            return;
        }

        frame->rax = 0;
        return;
    }

    case SYS_FORK: {
        /* sys_fork() → parent gets child PID, child gets 0 */
        struct process *parent = scheduler_get_current();

        /* 1. Allocate child PCB slot */
        struct process *child = process_alloc();
        if (!child) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* 2. Allocate child kernel stack */
        void *child_kstack = kmalloc(PROCESS_STACK_SIZE);
        if (!child_kstack) {
            frame->rax = (uint64_t)-1;
            return;
        }
        uint64_t child_kstack_top = (uint64_t)child_kstack + PROCESS_STACK_SIZE;

        /* 3. Clone parent address space (eager copy) */
        uint64_t child_cr3 = user_vm_fork_address_space(parent->cr3);
        if (!child_cr3) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* 4. Copy interrupt frame to child's kernel stack */
        uint64_t frame_size = sizeof(struct interrupt_frame);
        struct interrupt_frame *child_frame =
            (struct interrupt_frame *)(child_kstack_top - frame_size);
        memcpy(child_frame, frame, frame_size);

        /* 5. Child returns 0 from fork */
        child_frame->rax = 0;

        /* 6. Set up child PCB */
        strncpy(child->name, parent->name, sizeof(child->name) - 1);
        child->name[sizeof(child->name) - 1] = '\0';
        child->state = PROCESS_READY;
        child->kernel_stack = child_kstack_top;
        child->next = NULL;
        child->cr3 = child_cr3;
        child->is_user = true;
        child->user_program_data = parent->user_program_data;
        child->user_program_size = parent->user_program_size;
        child->ppid = parent->pid;
        child->exit_code = 0;
        child->wait_for_pid = 0;

        /* 7. Set context so scheduler jumps to fork_return_trampoline */
        memset(&child->context, 0, sizeof(struct context));
        child->context.rip = (uint64_t)fork_return_trampoline;
        child->context.rsp = (uint64_t)child_frame;

        /* 8. Add child to scheduler */
        scheduler_add(child);

        debug_printf("syscall: fork: parent PID=%u -> child PID=%u\n",
                     (uint64_t)parent->pid, (uint64_t)child->pid);

        /* 9. Parent returns child PID */
        frame->rax = (uint64_t)child->pid;
        return;
    }

    case SYS_WAIT: {
        /* sys_wait(int32_t *status) → returns child PID or -1 */
        int32_t *status_ptr = (int32_t *)arg1;
        struct process *cur = scheduler_get_current();

        /* Validate pointer if non-NULL */
        if (status_ptr && (uint64_t)status_ptr >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* Check for zombie child first */
        struct process *zombie = process_find_zombie_child(cur->pid);
        if (zombie) {
            uint32_t zpid = zombie->pid;
            if (status_ptr)
                *status_ptr = zombie->exit_code;
            zombie->state = PROCESS_TERMINATED;
            frame->rax = (uint64_t)zpid;
            return;
        }

        /* No zombie — do we have any living children? */
        if (!process_has_children(cur->pid)) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* Block until a child exits */
        cur->wait_for_pid = 0;  /* wait for any child */
        cur->state = PROCESS_BLOCKED;
        schedule();

        /* Re-enable interrupts after being rescheduled from PIT ISR */
        sti();

        /* Resumed — find and reap the zombie child */
        zombie = process_find_zombie_child(cur->pid);
        if (zombie) {
            uint32_t zpid = zombie->pid;
            if (status_ptr)
                *status_ptr = zombie->exit_code;
            zombie->state = PROCESS_TERMINATED;
            frame->rax = (uint64_t)zpid;
            return;
        }

        frame->rax = (uint64_t)-1;
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
