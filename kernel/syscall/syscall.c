#include "syscall.h"
#include "../arch/x86_64/isr.h"
#include "../arch/x86_64/fork_return.h"
#include "../drivers/vga.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../process/user_process.h"
#include "../memory/user_vm.h"
#include "../memory/vmm.h"
#include "../memory/kheap.h"
#include "../memory/mmap.h"
#include "../fs/vfs.h"
#include "../fs/ramfs.h"
#include "../fs/pipe.h"
#include "../fs/ext2.h"
#include "../fs/epoll.h"
#include "../fs/poll.h"
#include "../signal/signal.h"
#include "../drivers/tty.h"
#include "../drivers/device.h"
#include "../memory/user_access.h"
#include "../string.h"
#include "../debug/debug.h"
#include "../net/socket.h"
#include "../net/tcp.h"
#include "../elf/elf.h"
#include "../elf/elf_loader.h"
#include "../memory/pmm.h"

/* Initialize FD table for a process with console on 0/1/2 */
void process_fd_init(struct process *proc) {
    memset(proc->fd_table, 0, sizeof(proc->fd_table));
    for (int i = 0; i < 3; i++) {
        proc->fd_table[i].type = FD_CONSOLE;
        proc->fd_table[i].data = NULL;
        proc->fd_table[i].offset = 0;
    }
}

/* Find a free FD slot in a process (starting at 'start') */
static int process_fd_alloc(struct process *proc, int start) {
    for (int i = start; i < PROCESS_MAX_FDS; i++) {
        if (proc->fd_table[i].type == FD_NONE)
            return i;
    }
    return -1;
}

/*
 * Heavy syscall handlers are extracted into separate noinline functions
 * to prevent GCC -O2 from merging all switch-case locals into one
 * giant stack frame (which causes kernel stack overflow).
 */

__attribute__((noinline))
static void syscall_exit(struct interrupt_frame *frame, uint64_t arg1) {
    struct process *cur = scheduler_get_current();
    struct process *table = process_table_get();
    debug_printf("syscall: process %u (%s) exit with status %d\n",
                 (uint64_t)cur->pid, cur->name, arg1);

    /* Close all open FDs */
    for (int i = 0; i < PROCESS_MAX_FDS; i++) {
        if (cur->fd_table[i].type == FD_PIPE_READ) {
            pipe_close_read((struct pipe *)cur->fd_table[i].data);
            cur->fd_table[i].type = FD_NONE;
        } else if (cur->fd_table[i].type == FD_PIPE_WRITE) {
            pipe_close_write((struct pipe *)cur->fd_table[i].data);
            cur->fd_table[i].type = FD_NONE;
        } else if (cur->fd_table[i].type == FD_SOCKET) {
            socket_close((int)(uint64_t)cur->fd_table[i].data);
            cur->fd_table[i].type = FD_NONE;
        }
    }

    /* Free user address space */
    if (cur->cr3) {
        uint64_t old_cr3 = cur->cr3;
        cur->cr3 = 0;
        write_cr3(scheduler_get_kernel_cr3());
        user_vm_destroy_address_space(old_cr3);
    }

    cur->state = PROCESS_ZOMBIE;
    cur->exit_code = (int32_t)arg1;

    /* Send SIGCHLD to parent */
    signal_send(cur->ppid, 17); /* SIGCHLD */

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
}

__attribute__((noinline))
static void syscall_exec(struct interrupt_frame *frame, uint64_t arg1) {
    const char *path = (const char *)arg1;
    struct process *cur = scheduler_get_current();

    /* Validate pointer is in user space */
    if ((uint64_t)path >= KERNEL_VMA) {
        frame->rax = (uint64_t)-1;
        return;
    }

    /* Copy path to kernel buffer before we destroy the address space */
    char name_buf[32];
    strncpy(name_buf, path, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';

    /* Try ext2 filesystem first */
    uint64_t prog_size = 0;
    const uint8_t *prog_data = NULL;
    uint8_t *ext2_buf = NULL;

    if (ext2_is_mounted()) {
        char ext2_path[64];
        ext2_path[0] = '/';
        ext2_path[1] = 'b';
        ext2_path[2] = 'i';
        ext2_path[3] = 'n';
        ext2_path[4] = '/';
        strncpy(ext2_path + 5, name_buf, sizeof(ext2_path) - 6);
        ext2_path[sizeof(ext2_path) - 1] = '\0';

        uint32_t ino = ext2_path_lookup(ext2_path);
        if (ino) {
            struct ext2_inode inode;
            if (ext2_read_inode(ino, &inode) == 0 && inode.i_size > 0) {
                ext2_buf = (uint8_t *)kmalloc(inode.i_size);
                if (ext2_buf) {
                    int bytes = ext2_read_file(&inode, 0,
                                               inode.i_size, ext2_buf);
                    if (bytes > 0) {
                        prog_data = ext2_buf;
                        prog_size = (uint64_t)bytes;
                    }
                }
            }
        }
    }

    /* Fall back to RAMFS */
    if (!prog_data)
        prog_data = ramfs_get_file_data(name_buf, &prog_size);

    if (!prog_data) {
        debug_printf("syscall: exec '%s' not found\n", name_buf);
        frame->rax = (uint64_t)-1;
        return;
    }

    /* Validate ELF before destroying old address space (point of no return) */
    if (elf_validate(prog_data, prog_size) < 0) {
        debug_printf("syscall: exec '%s' invalid ELF\n", name_buf);
        frame->rax = (uint64_t)-1;
        return;
    }

    /* Create new address space */
    uint64_t new_cr3 = user_vm_create_address_space();
    if (!new_cr3) {
        debug_printf("syscall: exec '%s' failed to create address space\n", name_buf);
        frame->rax = (uint64_t)-1;
        return;
    }

    /* Load ELF segments into new address space */
    struct elf_load_result elf_result;
    if (elf_load(new_cr3, prog_data, prog_size, 0, &elf_result) < 0) {
        debug_printf("syscall: exec '%s' ELF load failed\n", name_buf);
        user_vm_destroy_address_space(new_cr3);
        frame->rax = (uint64_t)-1;
        return;
    }

    /* Allocate and map user stack page */
    uint64_t stack_phys = pmm_alloc_page();
    if (!stack_phys) {
        debug_printf("syscall: exec '%s' stack alloc failed\n", name_buf);
        user_vm_destroy_address_space(new_cr3);
        frame->rax = (uint64_t)-1;
        return;
    }
    memset(PHYS_TO_VIRT(stack_phys), 0, PAGE_SIZE);
    if (user_vm_map_page(new_cr3, USER_STACK_BOTTOM,
                         stack_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER) < 0) {
        pmm_free_page(stack_phys);
        user_vm_destroy_address_space(new_cr3);
        frame->rax = (uint64_t)-1;
        return;
    }

    /* Set up argv stack before destroying old address space */
    const char *exec_argv[1] = { name_buf };
    uint64_t entry_rsp, entry_argv_ptr;
    if (elf_setup_stack(stack_phys, USER_STACK_BOTTOM, 1, exec_argv,
                        &elf_result, &entry_rsp, &entry_argv_ptr) < 0) {
        user_vm_destroy_address_space(new_cr3);
        frame->rax = (uint64_t)-1;
        return;
    }

    /* === Point of no return === */

    /* Destroy old address space and switch to new one */
    vma_destroy_all(cur);
    uint64_t old_cr3 = cur->cr3;
    cur->cr3 = new_cr3;
    write_cr3(scheduler_get_kernel_cr3());
    if (old_cr3)
        user_vm_destroy_address_space(old_cr3);

    write_cr3(new_cr3);

    /* Reset signal handlers: custom handlers → SIG_DFL, keep SIG_IGN */
    for (int i = 0; i < 32; i++) {
        if (cur->sig_handlers[i] > 1) /* > SIG_IGN */
            cur->sig_handlers[i] = 0; /* SIG_DFL */
    }
    cur->sig_pending = 0;
    cur->in_signal_handler = false;

    /* Update process metadata */
    strncpy(cur->name, name_buf, sizeof(cur->name) - 1);
    cur->name[sizeof(cur->name) - 1] = '\0';
    cur->user_program_data = prog_data;
    cur->user_program_size = prog_size;

    /* Initialize brk for new image */
    cur->mmap_next   = MMAP_BASE;
    cur->brk_start   = elf_result.load_end;
    cur->brk_current = elf_result.load_end;

    /* Set up interrupt frame to jump to new entry point */
    frame->rip = elf_result.entry_point;
    frame->rsp = entry_rsp;
    frame->rax = 0;
    frame->rdi = 1;                /* argc */
    frame->rsi = entry_argv_ptr;   /* argv ptr */

    debug_printf("syscall: exec '%s' PID=%u entry=0x%x argc=1\n",
                 name_buf, (uint64_t)cur->pid, elf_result.entry_point);

    /* Return via IRETQ takes process to new entry point — never returns to old code */
}

/* sys_execv(path, argc, argv[]) — RAX=26
 * Replaces current process image with a new ELF, passing argv to _start.
 * arg1=RDI=path, arg2=RSI=argc, arg3=RDX=user_argv_ptr */
__attribute__((noinline))
static void syscall_execv(struct interrupt_frame *frame,
                          uint64_t arg1, uint64_t arg2, uint64_t arg3) {
#define EXECV_MAX_ARGS 32
#define EXECV_MAX_ARG_LEN 256
    const char *path = (const char *)arg1;
    int user_argc = (int)arg2;
    const char **user_argv = (const char **)arg3;
    struct process *cur = scheduler_get_current();

    /* Validate pointers */
    if ((uint64_t)path >= KERNEL_VMA || (uint64_t)user_argv >= KERNEL_VMA) {
        frame->rax = (uint64_t)-1;
        return;
    }
    if (user_argc < 0 || user_argc > EXECV_MAX_ARGS)
        user_argc = EXECV_MAX_ARGS;

    /* Copy path to kernel buffer */
    char name_buf[64];
    strncpy(name_buf, path, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';

    /* Extract basename for argv[0] */
    char *base = name_buf;
    for (char *p = name_buf; *p; p++)
        if (*p == '/') base = p + 1;

    /* Copy argv strings from user space into kernel buffers */
    char arg_storage[EXECV_MAX_ARGS][EXECV_MAX_ARG_LEN];
    const char *kern_argv[EXECV_MAX_ARGS];
    int argc = 0;

    /* argv[0] is always the basename of path */
    strncpy(arg_storage[0], base, EXECV_MAX_ARG_LEN - 1);
    arg_storage[0][EXECV_MAX_ARG_LEN - 1] = '\0';
    kern_argv[argc++] = arg_storage[0];

    /* Copy remaining argv from user space (skip user argv[0]) */
    for (int i = 1; i < user_argc && argc < EXECV_MAX_ARGS; i++) {
        uint64_t uptr = 0;
        /* Read the pointer from user argv array */
        const char **entry = user_argv + i;
        if ((uint64_t)entry >= KERNEL_VMA)
            break;
        uptr = (uint64_t)*entry;
        if (!uptr || uptr >= KERNEL_VMA)
            break;
        strncpy(arg_storage[argc], (const char *)uptr, EXECV_MAX_ARG_LEN - 1);
        arg_storage[argc][EXECV_MAX_ARG_LEN - 1] = '\0';
        kern_argv[argc] = arg_storage[argc];
        argc++;
    }

    /* Load program: try ext2 then RAMFS */
    uint64_t prog_size = 0;
    const uint8_t *prog_data = NULL;
    uint8_t *ext2_buf = NULL;

    if (ext2_is_mounted()) {
        char ext2_path[72];
        ext2_path[0] = '/'; ext2_path[1] = 'b'; ext2_path[2] = 'i';
        ext2_path[3] = 'n'; ext2_path[4] = '/';
        strncpy(ext2_path + 5, name_buf, sizeof(ext2_path) - 6);
        ext2_path[sizeof(ext2_path) - 1] = '\0';

        uint32_t ino = ext2_path_lookup(ext2_path);
        if (ino) {
            struct ext2_inode inode;
            if (ext2_read_inode(ino, &inode) == 0 && inode.i_size > 0) {
                ext2_buf = (uint8_t *)kmalloc(inode.i_size);
                if (ext2_buf) {
                    int bytes = ext2_read_file(&inode, 0, inode.i_size, ext2_buf);
                    if (bytes > 0) {
                        prog_data = ext2_buf;
                        prog_size = (uint64_t)bytes;
                    }
                }
            }
        }
    }
    if (!prog_data)
        prog_data = ramfs_get_file_data(name_buf, &prog_size);

    if (!prog_data) {
        debug_printf("syscall: execv '%s' not found\n", name_buf);
        frame->rax = (uint64_t)-1;
        return;
    }

    if (elf_validate(prog_data, prog_size) < 0) {
        frame->rax = (uint64_t)-1;
        return;
    }

    uint64_t new_cr3 = user_vm_create_address_space();
    if (!new_cr3) {
        frame->rax = (uint64_t)-1;
        return;
    }

    struct elf_load_result elf_result;
    if (elf_load(new_cr3, prog_data, prog_size, 0, &elf_result) < 0) {
        user_vm_destroy_address_space(new_cr3);
        frame->rax = (uint64_t)-1;
        return;
    }

    uint64_t stack_phys = pmm_alloc_page();
    if (!stack_phys) {
        user_vm_destroy_address_space(new_cr3);
        frame->rax = (uint64_t)-1;
        return;
    }
    memset(PHYS_TO_VIRT(stack_phys), 0, PAGE_SIZE);
    if (user_vm_map_page(new_cr3, USER_STACK_BOTTOM,
                         stack_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER) < 0) {
        pmm_free_page(stack_phys);
        user_vm_destroy_address_space(new_cr3);
        frame->rax = (uint64_t)-1;
        return;
    }

    uint64_t entry_rsp, entry_argv_ptr;
    if (elf_setup_stack(stack_phys, USER_STACK_BOTTOM, argc, kern_argv,
                        &elf_result, &entry_rsp, &entry_argv_ptr) < 0) {
        user_vm_destroy_address_space(new_cr3);
        frame->rax = (uint64_t)-1;
        return;
    }

    /* === Point of no return === */
    vma_destroy_all(cur);
    uint64_t old_cr3 = cur->cr3;
    cur->cr3 = new_cr3;
    write_cr3(scheduler_get_kernel_cr3());
    if (old_cr3)
        user_vm_destroy_address_space(old_cr3);
    write_cr3(new_cr3);

    for (int i = 0; i < 32; i++) {
        if (cur->sig_handlers[i] > 1)
            cur->sig_handlers[i] = 0;
    }
    cur->sig_pending = 0;
    cur->in_signal_handler = false;

    strncpy(cur->name, arg_storage[0], sizeof(cur->name) - 1);
    cur->name[sizeof(cur->name) - 1] = '\0';
    cur->user_program_data = prog_data;
    cur->user_program_size = prog_size;

    /* Initialize brk for new image */
    cur->mmap_next   = MMAP_BASE;
    cur->brk_start   = elf_result.load_end;
    cur->brk_current = elf_result.load_end;

    frame->rip = elf_result.entry_point;
    frame->rsp = entry_rsp;
    frame->rax = 0;
    frame->rdi = (uint64_t)argc;
    frame->rsi = entry_argv_ptr;

    debug_printf("syscall: execv '%s' PID=%u entry=0x%x argc=%d\n",
                 arg_storage[0], (uint64_t)cur->pid,
                 elf_result.entry_point, (uint64_t)argc);
}

__attribute__((noinline))
static void syscall_fork(struct interrupt_frame *frame) {
    struct process *parent = scheduler_get_current();

    /* 1. Allocate child PCB slot */
    struct process *child = process_alloc();
    if (!child) {
        frame->rax = (uint64_t)-1;
        return;
    }

    /* 2. Allocate child kernel stack with guard page */
    void *child_guard_and_stack = kmalloc_page_aligned(PAGE_SIZE + PROCESS_STACK_SIZE);
    if (!child_guard_and_stack) {
        frame->rax = (uint64_t)-1;
        return;
    }
    vmm_unmap_page((uint64_t)child_guard_and_stack);  /* Guard page */
    uint64_t child_kstack_top = (uint64_t)child_guard_and_stack + PAGE_SIZE + PROCESS_STACK_SIZE;

    /* 3. Clone parent address space (COW) */
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
    child->kernel_stack_guard = (uint64_t)child_guard_and_stack;
    child->kernel_stack = child_kstack_top;
    child->next = NULL;
    child->cr3 = child_cr3;
    child->is_user = true;
    child->user_program_data = parent->user_program_data;
    child->user_program_size = parent->user_program_size;
    child->ppid = parent->pid;
    child->exit_code = 0;
    child->wait_for_pid = 0;

    /* 7. Copy parent's FD table to child */
    memcpy(child->fd_table, parent->fd_table, sizeof(parent->fd_table));

    /* Increment refcounts for shared pipes and sockets */
    for (int i = 0; i < PROCESS_MAX_FDS; i++) {
        if (child->fd_table[i].type == FD_PIPE_READ ||
            child->fd_table[i].type == FD_PIPE_WRITE) {
            struct pipe *p = (struct pipe *)child->fd_table[i].data;
            if (p) {
                pipe_inc_ref(p, child->fd_table[i].type);
            }
        } else if (child->fd_table[i].type == FD_SOCKET) {
            socket_inc_ref((int)(uint64_t)child->fd_table[i].data);
        }
    }

    /* 8. Copy signal handlers */
    memcpy(child->sig_handlers, parent->sig_handlers,
           sizeof(parent->sig_handlers));
    child->sig_pending = 0;
    child->in_signal_handler = false;

    /* Copy VMA table and brk */
    vma_fork_copy(parent, child);

    /* Initialize epoll state */
    child->epoll_fd_idx = -1;

    /* 9. Set context so scheduler jumps to fork_return_trampoline */
    memset(&child->context, 0, sizeof(struct context));
    child->context.rip = (uint64_t)fork_return_trampoline;
    child->context.rsp = (uint64_t)child_frame;

    /* 10. Add child to scheduler */
    scheduler_add(child);

    debug_printf("syscall: fork: parent PID=%u -> child PID=%u\n",
                 (uint64_t)parent->pid, (uint64_t)child->pid);

    /* 11. Parent returns child PID */
    frame->rax = (uint64_t)child->pid;
}

/* SYS_BRK (30): new_brk = 0 -> return current brk; else set new brk */
__attribute__((noinline))
static uint64_t syscall_brk(uint64_t new_brk) {
    struct process *proc = scheduler_get_current();
    if (!proc || !proc->is_user) return (uint64_t)-1;

    if (new_brk == 0)
        return proc->brk_current;

    /* new_brk must be >= brk_start */
    if (new_brk < proc->brk_start)
        return proc->brk_current;

    uint64_t old_end = (proc->brk_current + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t new_end = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /* Grow: map new pages */
    for (uint64_t pg = old_end; pg < new_end; pg += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return proc->brk_current;
        memset((void *)PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        if (user_vm_map_page(proc->cr3, pg, phys,
                             PTE_PRESENT | PTE_WRITABLE | PTE_USER) < 0) {
            pmm_page_unref(phys);
            return proc->brk_current;
        }
    }

    proc->brk_current = new_brk;
    return new_brk;
}

/* SYS_MMAP (27): arg1 = struct mmap_args* */
__attribute__((noinline))
static uint64_t syscall_mmap(uint64_t args_ptr) {
    struct process *proc = scheduler_get_current();
    if (!proc || !proc->is_user) return (uint64_t)-1;

    if (args_ptr >= KERNEL_VMA) return (uint64_t)-1;

    struct mmap_args args;
    /* Copy from user space */
    memcpy(&args, (void *)args_ptr, sizeof(args));

    if (args.len == 0) return (uint64_t)-1;
    uint64_t len = (args.len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    uint64_t addr;
    if ((args.flags & MAP_FIXED) && args.addr != 0) {
        addr = args.addr & ~(PAGE_SIZE - 1);
    } else {
        addr = vma_find_free_range(proc, args.addr, len);
        if (!addr) return (uint64_t)-1;
    }

    vma_t *vma = vma_alloc(proc);
    if (!vma) return (uint64_t)-1;

    vma->start = addr;
    vma->end   = addr + len;
    vma->prot  = args.prot;
    vma->flags = args.flags;
    vma->in_use = true;
    vma->shared_phys = 0;

    if (args.flags & MAP_ANONYMOUS) {
        vma->type = VMA_ANON;
        vma->file_inode = 0;
        vma->file_offset = 0;
    } else {
        /* File-backed mmap */
        vma->type = VMA_FILE;
        vma->file_offset = args.offset;
        /* Look up inode from fd */
        if (args.fd < 0 || args.fd >= PROCESS_MAX_FDS) {
            vma->in_use = false;
            return (uint64_t)-1;
        }
        struct process_fd *pfd = &proc->fd_table[args.fd];
        if (pfd->type == FD_EXT2) {
            vma->file_inode = (uint32_t)(uint64_t)pfd->data;
        } else {
            vma->in_use = false;
            return (uint64_t)-1;
        }
    }

    /* Advance mmap_next hint */
    if (addr + len > proc->mmap_next)
        proc->mmap_next = addr + len;

    debug_printf("mmap: PID=%u addr=0x%x len=0x%x prot=0x%x flags=0x%x\n",
                 (uint64_t)proc->pid, addr, len, (uint64_t)args.prot, (uint64_t)args.flags);
    return addr;
}

/* SYS_MUNMAP (28) */
static uint64_t syscall_munmap(uint64_t addr, uint64_t len) {
    struct process *proc = scheduler_get_current();
    if (!proc || !proc->is_user) return (uint64_t)-1;
    return (uint64_t)vma_unmap(proc, addr, len);
}

/* SYS_MPROTECT (29) */
static uint64_t syscall_mprotect(uint64_t addr, uint64_t len, uint64_t prot) {
    struct process *proc = scheduler_get_current();
    if (!proc || !proc->is_user) return (uint64_t)-1;
    return (uint64_t)vma_protect(proc, addr, len, (uint32_t)prot);
}

/* epoll_ctl args struct */
struct epoll_ctl_args {
    int32_t  op;
    int32_t  fd;
    uint32_t events;
    uint32_t pad;
    uint64_t data;
};

/* SYS_EPOLL_CREATE (31) */
static uint64_t syscall_epoll_create(void) {
    struct process *proc = scheduler_get_current();
    if (!proc || !proc->is_user) return (uint64_t)-1;

    int idx = epoll_create_instance(proc->pid);
    if (idx < 0) return (uint64_t)-1;

    int fd = process_fd_alloc(proc, 3);
    if (fd < 0) {
        epoll_destroy(idx);
        return (uint64_t)-1;
    }

    proc->fd_table[fd].type   = FD_EPOLL;
    proc->fd_table[fd].data   = (void *)(uint64_t)idx;
    proc->fd_table[fd].offset = 0;
    return (uint64_t)fd;
}

/* SYS_EPOLL_CTL (32): arg1=epfd, arg2=struct epoll_ctl_args* */
__attribute__((noinline))
static uint64_t syscall_epoll_ctl(uint64_t epfd, uint64_t args_ptr) {
    struct process *proc = scheduler_get_current();
    if (!proc || !proc->is_user) return (uint64_t)-1;
    if (args_ptr >= KERNEL_VMA) return (uint64_t)-1;

    if (epfd >= PROCESS_MAX_FDS || proc->fd_table[epfd].type != FD_EPOLL)
        return (uint64_t)-1;

    int epoll_idx = (int)(uint64_t)proc->fd_table[epfd].data;

    struct epoll_ctl_args args;
    memcpy(&args, (void *)args_ptr, sizeof(args));

    return (uint64_t)epoll_ctl_instance(epoll_idx, args.op, args.fd,
                                         args.events, args.data);
}

/* epoll_wait args struct */
struct epoll_wait_args {
    int32_t  maxevents;
    int32_t  timeout_ms;
    uint64_t events_ptr;  /* user-space struct epoll_event[] */
};

/* SYS_EPOLL_WAIT (33): arg1=epfd, arg2=struct epoll_wait_args* */
__attribute__((noinline))
static uint64_t syscall_epoll_wait(uint64_t epfd, uint64_t args_ptr) {
    struct process *proc = scheduler_get_current();
    if (!proc || !proc->is_user) return (uint64_t)-1;
    if (args_ptr >= KERNEL_VMA) return (uint64_t)-1;

    if (epfd >= PROCESS_MAX_FDS || proc->fd_table[epfd].type != FD_EPOLL)
        return (uint64_t)-1;

    int epoll_idx = (int)(uint64_t)proc->fd_table[epfd].data;
    struct epoll_instance *ep = epoll_get(epoll_idx);
    if (!ep) return (uint64_t)-1;

    struct epoll_wait_args args;
    memcpy(&args, (void *)args_ptr, sizeof(args));

    if (args.maxevents <= 0) return (uint64_t)-1;
    if (args.events_ptr >= KERNEL_VMA) return (uint64_t)-1;

    struct epoll_event *user_events = (struct epoll_event *)args.events_ptr;

    /* Quick poll: return immediately if any fd is ready */
    int found = 0;
    for (int j = 0; j < ep->watch_count && found < args.maxevents; j++) {
        int wfd = ep->watches[j].fd;
        uint32_t wev = ep->watches[j].events;
        uint32_t ready = 0;
        if ((wev & EPOLLIN) && fd_is_readable(proc, wfd))   ready |= EPOLLIN;
        if ((wev & EPOLLOUT) && fd_is_writable(proc, wfd))  ready |= EPOLLOUT;
        if (ready) {
            user_events[found].events = ready;
            user_events[found].data   = ep->watches[j].data;
            found++;
        }
    }
    if (found > 0 || args.timeout_ms == 0)
        return (uint64_t)found;

    /* Block until scheduler_tick wakes us (timeout or fd readiness).
     * INT 0x80 is an interrupt gate (IF=0), so no race between setup and block. */
    uint64_t timeout_ticks = (args.timeout_ms < 0)
        ? (uint64_t)-1
        : (uint64_t)((uint32_t)args.timeout_ms / 10 + 1);

    proc->epoll_fd_idx        = epoll_idx;
    proc->epoll_timeout_ticks = timeout_ticks;
    proc->state               = PROCESS_BLOCKED;
    schedule();
    sti();

    /* Resumed by scheduler_tick: re-scan for ready fds */
    proc->epoll_fd_idx = -1;
    found = 0;
    for (int j = 0; j < ep->watch_count && found < args.maxevents; j++) {
        int wfd = ep->watches[j].fd;
        uint32_t wev = ep->watches[j].events;
        uint32_t ready = 0;
        if ((wev & EPOLLIN) && fd_is_readable(proc, wfd))   ready |= EPOLLIN;
        if ((wev & EPOLLOUT) && fd_is_writable(proc, wfd))  ready |= EPOLLOUT;
        if (ready) {
            user_events[found].events = ready;
            user_events[found].data   = ep->watches[j].data;
            found++;
        }
    }
    return (uint64_t)found;
}

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

        /* Validate buffer is in user space (below KERNEL_VMA) */
        if ((uint64_t)buf >= KERNEL_VMA || (uint64_t)buf + len >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        struct process *cur = scheduler_get_current();

        if (fd >= PROCESS_MAX_FDS || cur->fd_table[fd].type == FD_NONE) {
            frame->rax = (uint64_t)-1;
            return;
        }

        int fdtype = cur->fd_table[fd].type;

        if (fdtype == FD_CONSOLE) {
            /* stdout/stderr: write through TTY */
            int ret = tty_write((const uint8_t *)buf, (uint32_t)len);
            frame->rax = (uint64_t)ret;
        } else if (fdtype == FD_PIPE_WRITE) {
            int ret = pipe_write((struct pipe *)cur->fd_table[fd].data,
                                 (const uint8_t *)buf, (uint32_t)len);
            frame->rax = (uint64_t)ret;
        } else if (fdtype == FD_DEVICE) {
            struct device *dev = (struct device *)cur->fd_table[fd].data;
            if (dev && dev->write) {
                int ret = dev->write(dev, (const uint8_t *)buf, (uint32_t)len);
                frame->rax = (uint64_t)ret;
            } else {
                frame->rax = (uint64_t)-1;
            }
        } else if (fdtype == FD_VFS) {
            struct vfs_node *node = (struct vfs_node *)cur->fd_table[fd].data;
            if (node && node->ops && node->ops->write) {
                int ret = node->ops->write(node, cur->fd_table[fd].offset,
                                           len, (const uint8_t *)buf);
                if (ret > 0)
                    cur->fd_table[fd].offset += (uint64_t)ret;
                frame->rax = (uint64_t)ret;
            } else {
                frame->rax = (uint64_t)-1;
            }
        } else if (fdtype == FD_EXT2) {
            uint32_t ino = (uint32_t)(uint64_t)cur->fd_table[fd].data;
            struct ext2_inode inode;
            if (ext2_read_inode(ino, &inode) == 0) {
                int ret = ext2_write_file(ino, &inode,
                                          cur->fd_table[fd].offset,
                                          len, (const uint8_t *)buf);
                if (ret > 0)
                    cur->fd_table[fd].offset += (uint64_t)ret;
                frame->rax = (uint64_t)ret;
            } else {
                frame->rax = (uint64_t)-1;
            }
        } else if (fdtype == FD_SOCKET) {
            int sock_idx = (int)(uint64_t)cur->fd_table[fd].data;
            int ret = socket_send(sock_idx, (const uint8_t *)buf, (uint32_t)len);
            frame->rax = (uint64_t)ret;
        } else {
            frame->rax = (uint64_t)-1;
        }
        return;
    }

    case SYS_EXIT:
        syscall_exit(frame, arg1);
        return;

    case SYS_GETPID: {
        struct process *cur = scheduler_get_current();
        frame->rax = (uint64_t)cur->pid;
        return;
    }

    case SYS_EXEC:
        syscall_exec(frame, arg1);
        return;

    case SYS_FORK:
        syscall_fork(frame);
        return;

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
            zombie->state = PROCESS_UNUSED;
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
            zombie->state = PROCESS_UNUSED;
            frame->rax = (uint64_t)zpid;
            return;
        }

        frame->rax = (uint64_t)-1;
        return;
    }

    case SYS_READ: {
        /* sys_read(fd, buf, count) */
        uint64_t fd = arg1;
        uint8_t *buf = (uint8_t *)arg2;
        uint64_t count = arg3;

        if ((uint64_t)buf >= KERNEL_VMA || (uint64_t)buf + count >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        struct process *cur = scheduler_get_current();

        if (fd >= PROCESS_MAX_FDS || cur->fd_table[fd].type == FD_NONE) {
            frame->rax = (uint64_t)-1;
            return;
        }

        int fdtype = cur->fd_table[fd].type;

        if (fdtype == FD_PIPE_READ) {
            int ret = pipe_read((struct pipe *)cur->fd_table[fd].data,
                                buf, (uint32_t)count);
            frame->rax = (uint64_t)ret;
        } else if (fdtype == FD_VFS) {
            struct vfs_node *node = (struct vfs_node *)cur->fd_table[fd].data;
            if (node && node->ops && node->ops->read) {
                int ret = node->ops->read(node, cur->fd_table[fd].offset,
                                          count, buf);
                if (ret > 0)
                    cur->fd_table[fd].offset += (uint64_t)ret;
                frame->rax = (uint64_t)ret;
            } else {
                frame->rax = (uint64_t)-1;
            }
        } else if (fdtype == FD_DEVICE) {
            struct device *dev = (struct device *)cur->fd_table[fd].data;
            if (dev && dev->read) {
                int ret = dev->read(dev, buf, (uint32_t)count);
                frame->rax = (uint64_t)ret;
            } else {
                frame->rax = (uint64_t)-1;
            }
        } else if (fdtype == FD_EXT2) {
            uint32_t ino = (uint32_t)(uint64_t)cur->fd_table[fd].data;
            struct ext2_inode inode;
            if (ext2_read_inode(ino, &inode) == 0) {
                int ret = ext2_read_file(&inode, cur->fd_table[fd].offset,
                                         count, buf);
                if (ret > 0)
                    cur->fd_table[fd].offset += (uint64_t)ret;
                frame->rax = (uint64_t)ret;
            } else {
                frame->rax = (uint64_t)-1;
            }
        } else if (fdtype == FD_CONSOLE) {
            /* stdin: read from TTY */
            int ret = tty_read(buf, (uint32_t)count);
            frame->rax = (uint64_t)ret;
        } else if (fdtype == FD_SOCKET) {
            int sock_idx = (int)(uint64_t)cur->fd_table[fd].data;
            int ret = socket_recv(sock_idx, buf, (uint32_t)count);
            frame->rax = (uint64_t)ret;
        } else {
            frame->rax = (uint64_t)-1;
        }
        return;
    }

    case SYS_OPEN: {
        /* sys_open(path, flags) */
        const char *path = (const char *)arg1;
        uint64_t flags = arg2;

        if ((uint64_t)path >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        struct process *cur = scheduler_get_current();

        /* Check for /dev/ paths -> device FD */
        if (strncmp(path, "/dev/", 5) == 0) {
            const char *devname = path + 5;
            struct device *dev = device_find(devname);
            if (!dev) {
                frame->rax = (uint64_t)-1;
                return;
            }
            int fd = process_fd_alloc(cur, 3);
            if (fd < 0) {
                frame->rax = (uint64_t)-1;
                return;
            }
            cur->fd_table[fd].type = FD_DEVICE;
            cur->fd_table[fd].data = dev;
            cur->fd_table[fd].offset = 0;
            frame->rax = (uint64_t)fd;
            return;
        }

        /* Check mount points first (e.g., /proc) */
        struct vfs_ops *mount_ops = vfs_get_mount_ops(path);
        struct vfs_node *node;

        if (mount_ops) {
            /* For mounted filesystems, create a temporary node */
            node = vfs_register_node(path, VFS_FILE);
            if (!node) {
                frame->rax = (uint64_t)-1;
                return;
            }
            node->ops = mount_ops;
        } else {
            node = vfs_lookup(path);
            if (!node) {
                /* Try ext2 filesystem */
                if (ext2_is_mounted()) {
                    uint32_t ino = ext2_path_lookup(path);
                    if (ino) {
                        /* Found on ext2 — create FD_EXT2 */
                        int fd = process_fd_alloc(cur, 3);
                        if (fd < 0) {
                            frame->rax = (uint64_t)-1;
                            return;
                        }
                        cur->fd_table[fd].type = FD_EXT2;
                        cur->fd_table[fd].data = (void *)(uint64_t)ino;
                        cur->fd_table[fd].offset = 0;
                        frame->rax = (uint64_t)fd;
                        return;
                    }

                    /* O_CREAT: create file on ext2 if not found */
                    if (flags & 0x40) {
                        char pathbuf[256];
                        strncpy(pathbuf, path, sizeof(pathbuf) - 1);
                        pathbuf[sizeof(pathbuf) - 1] = '\0';
                        char *last_slash = NULL;
                        for (char *p = pathbuf; *p; p++) {
                            if (*p == '/') last_slash = p;
                        }
                        uint32_t parent_ino = EXT2_ROOT_INODE;
                        const char *fname = pathbuf;
                        if (last_slash && last_slash != pathbuf) {
                            *last_slash = '\0';
                            parent_ino = ext2_path_lookup(pathbuf);
                            fname = last_slash + 1;
                        } else if (last_slash == pathbuf) {
                            fname = pathbuf + 1;
                        }
                        if (parent_ino && *fname) {
                            uint32_t new_ino = ext2_create(parent_ino, fname,
                                                           EXT2_S_IFREG | 0644);
                            if (new_ino) {
                                int fd = process_fd_alloc(cur, 3);
                                if (fd < 0) {
                                    frame->rax = (uint64_t)-1;
                                    return;
                                }
                                cur->fd_table[fd].type = FD_EXT2;
                                cur->fd_table[fd].data = (void *)(uint64_t)new_ino;
                                cur->fd_table[fd].offset = 0;
                                frame->rax = (uint64_t)fd;
                                return;
                            }
                        }
                    }
                }
                frame->rax = (uint64_t)-1;
                return;
            }
        }

        /* Allocate FD in process table */
        int fd = process_fd_alloc(cur, 3);
        if (fd < 0) {
            frame->rax = (uint64_t)-1;
            return;
        }

        cur->fd_table[fd].type = FD_VFS;
        cur->fd_table[fd].data = node;
        cur->fd_table[fd].offset = 0;

        frame->rax = (uint64_t)fd;
        return;
    }

    case SYS_CLOSE: {
        /* sys_close(fd) */
        uint64_t fd = arg1;
        struct process *cur = scheduler_get_current();

        if (fd >= PROCESS_MAX_FDS || fd < 3) {
            frame->rax = (uint64_t)-1;
            return;
        }

        int fdtype = cur->fd_table[fd].type;
        if (fdtype == FD_NONE) {
            frame->rax = (uint64_t)-1;
            return;
        }

        if (fdtype == FD_PIPE_READ)
            pipe_close_read((struct pipe *)cur->fd_table[fd].data);
        else if (fdtype == FD_PIPE_WRITE)
            pipe_close_write((struct pipe *)cur->fd_table[fd].data);
        else if (fdtype == FD_SOCKET)
            socket_close((int)(uint64_t)cur->fd_table[fd].data);
        else if (fdtype == FD_EPOLL)
            epoll_destroy((int)(uint64_t)cur->fd_table[fd].data);

        cur->fd_table[fd].type = FD_NONE;
        cur->fd_table[fd].data = NULL;
        cur->fd_table[fd].offset = 0;

        frame->rax = 0;
        return;
    }

    case SYS_PIPE: {
        /* sys_pipe(int fds[2]) */
        int *user_fds = (int *)arg1;

        if ((uint64_t)user_fds >= KERNEL_VMA ||
            (uint64_t)(user_fds + 2) >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        struct process *cur = scheduler_get_current();
        struct pipe *p = NULL;

        if (pipe_create(&p) < 0 || !p) {
            frame->rax = (uint64_t)-1;
            return;
        }

        int rfd = process_fd_alloc(cur, 3);
        if (rfd < 0) {
            frame->rax = (uint64_t)-1;
            return;
        }
        cur->fd_table[rfd].type = FD_PIPE_READ;
        cur->fd_table[rfd].data = p;

        int wfd = process_fd_alloc(cur, rfd + 1);
        if (wfd < 0) {
            cur->fd_table[rfd].type = FD_NONE;
            frame->rax = (uint64_t)-1;
            return;
        }
        cur->fd_table[wfd].type = FD_PIPE_WRITE;
        cur->fd_table[wfd].data = p;

        user_fds[0] = rfd;
        user_fds[1] = wfd;
        frame->rax = 0;
        return;
    }

    case SYS_DUP2: {
        /* sys_dup2(oldfd, newfd) */
        uint64_t oldfd = arg1;
        uint64_t newfd = arg2;
        struct process *cur = scheduler_get_current();

        if (oldfd >= PROCESS_MAX_FDS || newfd >= PROCESS_MAX_FDS) {
            frame->rax = (uint64_t)-1;
            return;
        }
        if (cur->fd_table[oldfd].type == FD_NONE) {
            frame->rax = (uint64_t)-1;
            return;
        }
        if (oldfd == newfd) {
            frame->rax = (uint64_t)newfd;
            return;
        }

        /* Close newfd if open */
        if (cur->fd_table[newfd].type == FD_PIPE_READ)
            pipe_close_read((struct pipe *)cur->fd_table[newfd].data);
        else if (cur->fd_table[newfd].type == FD_PIPE_WRITE)
            pipe_close_write((struct pipe *)cur->fd_table[newfd].data);

        /* Copy oldfd to newfd */
        cur->fd_table[newfd] = cur->fd_table[oldfd];

        /* Increment pipe refcount if pipe */
        if (cur->fd_table[newfd].type == FD_PIPE_READ ||
            cur->fd_table[newfd].type == FD_PIPE_WRITE) {
            pipe_inc_ref((struct pipe *)cur->fd_table[newfd].data,
                         cur->fd_table[newfd].type);
        }

        frame->rax = (uint64_t)newfd;
        return;
    }

    case SYS_KILL: {
        /* sys_kill(pid, sig) */
        uint32_t pid = (uint32_t)arg1;
        int sig = (int)arg2;

        if (sig < 1 || sig >= 32) {
            frame->rax = (uint64_t)-1;
            return;
        }

        struct process *target = process_get_by_pid(pid);
        if (!target || target->state == PROCESS_UNUSED ||
            target->state == PROCESS_TERMINATED) {
            frame->rax = (uint64_t)-1;
            return;
        }

        signal_send(pid, sig);
        frame->rax = 0;
        return;
    }

    case SYS_SIGACTION: {
        /* sys_sigaction(sig, handler) */
        int sig = (int)arg1;
        uint64_t handler = arg2;
        struct process *cur = scheduler_get_current();

        if (sig < 1 || sig >= 32 || sig == 9) { /* Can't catch SIGKILL */
            frame->rax = (uint64_t)-1;
            return;
        }

        cur->sig_handlers[sig] = handler;
        frame->rax = 0;
        return;
    }

    case SYS_SIGRETURN: {
        /* Restore pre-signal user context */
        struct process *cur = scheduler_get_current();

        if (!cur->in_signal_handler) {
            frame->rax = (uint64_t)-1;
            return;
        }

        frame->rip = cur->sig_saved_rip;
        frame->rsp = cur->sig_saved_rsp;
        frame->rflags = cur->sig_saved_rflags;
        frame->rax = cur->sig_saved_rax;
        frame->rdi = cur->sig_saved_rdi;
        frame->rsi = cur->sig_saved_rsi;
        frame->rdx = cur->sig_saved_rdx;
        cur->in_signal_handler = false;
        return;
    }

    case SYS_GETPPID: {
        struct process *cur = scheduler_get_current();
        frame->rax = (uint64_t)cur->ppid;
        return;
    }

    case SYS_GETDENTS: {
        /* sys_getdents(fd, buf, size) - read directory entries
         * buf format: repeated [uint32_t inode, uint8_t name_len, char name[]] */
        uint64_t fd = arg1;
        uint8_t *buf = (uint8_t *)arg2;
        uint64_t buf_size = arg3;

        if ((uint64_t)buf >= KERNEL_VMA || (uint64_t)buf + buf_size >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        struct process *cur = scheduler_get_current();
        if (fd >= PROCESS_MAX_FDS || cur->fd_table[fd].type == FD_NONE) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* Use fd offset as directory index counter */
        int fdtype = cur->fd_table[fd].type;
        uint32_t bytes_written = 0;

        if (fdtype == FD_VFS) {
            struct vfs_node *node = (struct vfs_node *)cur->fd_table[fd].data;
            if (node && node->ops && node->ops->readdir) {
                char name[32];
                uint32_t idx = (uint32_t)cur->fd_table[fd].offset;
                while (bytes_written + 37 < buf_size) {
                    if (node->ops->readdir(node, idx, name, sizeof(name)) < 0)
                        break;
                    uint8_t nlen = (uint8_t)strlen(name);
                    uint32_t entry_size = 5 + nlen;  /* inode(4) + name_len(1) + name */
                    if (bytes_written + entry_size > buf_size)
                        break;
                    /* inode = 0 (unknown) */
                    memset(buf + bytes_written, 0, 4);
                    buf[bytes_written + 4] = nlen;
                    memcpy(buf + bytes_written + 5, name, nlen);
                    bytes_written += entry_size;
                    idx++;
                }
                cur->fd_table[fd].offset = (uint64_t)idx;
            } else if (node) {
                /* Root "/" — RAMFS has no readdir; enumerate VFS nodes + ext2 */
                char name[32];
                uint32_t idx = 0;
                while (bytes_written + 37 < buf_size) {
                    if (vfs_readdir("/", idx, name, sizeof(name)) < 0)
                        break;
                    uint8_t nlen = (uint8_t)strlen(name);
                    uint32_t entry_size = 5 + nlen;
                    if (bytes_written + entry_size > buf_size)
                        break;
                    memset(buf + bytes_written, 0, 4);
                    buf[bytes_written + 4] = nlen;
                    memcpy(buf + bytes_written + 5, name, nlen);
                    bytes_written += entry_size;
                    idx++;
                }
                /* Also include ext2 root entries */
                if (ext2_is_mounted()) {
                    struct ext2_inode root_inode;
                    if (ext2_read_inode(EXT2_ROOT_INODE, &root_inode) == 0) {
                        static uint8_t dir_buf[4096];
                        uint32_t dir_size = root_inode.i_size;
                        if (dir_size > sizeof(dir_buf))
                            dir_size = sizeof(dir_buf);
                        int rd = ext2_read_file(&root_inode, 0, dir_size, dir_buf);
                        if (rd > 0) {
                            uint32_t pos = 0;
                            while (pos < (uint32_t)rd) {
                                struct ext2_dir_entry *de =
                                    (struct ext2_dir_entry *)(dir_buf + pos);
                                if (de->rec_len == 0) break;
                                if (de->inode != 0 && de->name_len > 0) {
                                    char *de_name = (char *)(dir_buf + pos +
                                                    sizeof(struct ext2_dir_entry));
                                    uint8_t nlen2 = de->name_len;
                                    /* Skip . and .. */
                                    if (!(nlen2 == 1 && de_name[0] == '.') &&
                                        !(nlen2 == 2 && de_name[0] == '.' && de_name[1] == '.')) {
                                        uint32_t entry_size = 5 + nlen2;
                                        if (bytes_written + entry_size > buf_size)
                                            break;
                                        memcpy(buf + bytes_written, &de->inode, 4);
                                        buf[bytes_written + 4] = nlen2;
                                        memcpy(buf + bytes_written + 5, de_name, nlen2);
                                        bytes_written += entry_size;
                                    }
                                }
                                pos += de->rec_len;
                            }
                        }
                    }
                }
            }
        } else if (fdtype == FD_EXT2) {
            /* Read ext2 directory entries */
            uint32_t ino = (uint32_t)(uint64_t)cur->fd_table[fd].data;
            struct ext2_inode inode;
            if (ext2_read_inode(ino, &inode) == 0 &&
                (inode.i_mode & EXT2_S_IFDIR)) {
                /* Read directory data into a temp buffer */
                uint32_t dir_size = inode.i_size;
                static uint8_t dir_buf2[4096];
                if (dir_size > sizeof(dir_buf2))
                    dir_size = sizeof(dir_buf2);
                int rd = ext2_read_file(&inode, 0, dir_size, dir_buf2);
                if (rd > 0) {
                    uint32_t pos = 0;
                    while (pos < (uint32_t)rd) {
                        struct ext2_dir_entry *de =
                            (struct ext2_dir_entry *)(dir_buf2 + pos);
                        if (de->rec_len == 0)
                            break;
                        if (de->inode != 0 && de->name_len > 0) {
                            char *de_name = (char *)(dir_buf2 + pos +
                                            sizeof(struct ext2_dir_entry));
                            uint8_t nlen = de->name_len;
                            uint32_t entry_size = 5 + nlen;
                            if (bytes_written + entry_size > buf_size)
                                break;
                            /* inode number */
                            memcpy(buf + bytes_written, &de->inode, 4);
                            buf[bytes_written + 4] = nlen;
                            memcpy(buf + bytes_written + 5, de_name, nlen);
                            bytes_written += entry_size;
                        }
                        pos += de->rec_len;
                    }
                }
            }
        }

        frame->rax = (uint64_t)bytes_written;
        return;
    }

    case SYS_STAT: {
        /* sys_stat(path, stat_buf) */
        const char *path = (const char *)arg1;
        uint8_t *sbuf = (uint8_t *)arg2;

        if ((uint64_t)path >= KERNEL_VMA || (uint64_t)sbuf >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* Try ext2 first */
        if (ext2_is_mounted()) {
            uint32_t ino = ext2_path_lookup(path);
            if (ino) {
                struct ext2_inode inode;
                if (ext2_read_inode(ino, &inode) == 0) {
                    /* type (uint32_t) */
                    uint32_t ftype = (inode.i_mode & EXT2_S_IFDIR) ? 2 : 1;
                    memcpy(sbuf, &ftype, 4);
                    /* size (uint32_t) */
                    memcpy(sbuf + 4, &inode.i_size, 4);
                    /* inode (uint32_t) */
                    memcpy(sbuf + 8, &ino, 4);
                    frame->rax = 0;
                    return;
                }
            }
        }

        /* Try VFS */
        struct vfs_node *node = vfs_lookup(path);
        if (node) {
            uint32_t ftype = (node->type == VFS_DIRECTORY) ? 2 : 1;
            memcpy(sbuf, &ftype, 4);
            uint32_t fsize = (uint32_t)node->size;
            memcpy(sbuf + 4, &fsize, 4);
            uint32_t zero = 0;
            memcpy(sbuf + 8, &zero, 4);
            frame->rax = 0;
            return;
        }

        frame->rax = (uint64_t)-1;
        return;
    }

    case SYS_MKDIR: {
        /* sys_mkdir(path) */
        const char *path = (const char *)arg1;

        if ((uint64_t)path >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        if (!ext2_is_mounted()) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* Split path into parent + name */
        char pathbuf[256];
        strncpy(pathbuf, path, sizeof(pathbuf) - 1);
        pathbuf[sizeof(pathbuf) - 1] = '\0';

        char *last_slash = NULL;
        for (char *p = pathbuf; *p; p++) {
            if (*p == '/') last_slash = p;
        }

        uint32_t parent_ino = EXT2_ROOT_INODE;
        const char *dirname = pathbuf;
        if (last_slash && last_slash != pathbuf) {
            *last_slash = '\0';
            parent_ino = ext2_path_lookup(pathbuf);
            dirname = last_slash + 1;
        } else if (last_slash == pathbuf) {
            dirname = pathbuf + 1;
        }

        if (!parent_ino || !*dirname) {
            frame->rax = (uint64_t)-1;
            return;
        }

        uint32_t ino = ext2_mkdir(parent_ino, dirname);
        frame->rax = ino ? 0 : (uint64_t)-1;
        return;
    }

    case SYS_UNLINK: {
        /* sys_unlink(path) */
        const char *path = (const char *)arg1;

        if ((uint64_t)path >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        if (!ext2_is_mounted()) {
            frame->rax = (uint64_t)-1;
            return;
        }

        char pathbuf[256];
        strncpy(pathbuf, path, sizeof(pathbuf) - 1);
        pathbuf[sizeof(pathbuf) - 1] = '\0';

        char *last_slash = NULL;
        for (char *p = pathbuf; *p; p++) {
            if (*p == '/') last_slash = p;
        }

        uint32_t parent_ino = EXT2_ROOT_INODE;
        const char *fname = pathbuf;
        if (last_slash && last_slash != pathbuf) {
            *last_slash = '\0';
            parent_ino = ext2_path_lookup(pathbuf);
            fname = last_slash + 1;
        } else if (last_slash == pathbuf) {
            fname = pathbuf + 1;
        }

        if (!parent_ino || !*fname) {
            frame->rax = (uint64_t)-1;
            return;
        }

        frame->rax = (uint64_t)ext2_unlink(parent_ino, fname);
        return;
    }

    case SYS_SOCKET: {
        /* sys_socket(domain, type, protocol) — only AF_INET supported */
        int type = (int)arg2;
        if ((int)arg1 != AF_INET || (type != SOCK_STREAM && type != SOCK_DGRAM)) {
            frame->rax = (uint64_t)-1;
            return;
        }
        int sock_idx = socket_create(type);
        if (sock_idx < 0) {
            frame->rax = (uint64_t)-1;
            return;
        }
        struct process *cur = scheduler_get_current();
        int fd = process_fd_alloc(cur, 3);
        if (fd < 0) {
            socket_close(sock_idx);
            frame->rax = (uint64_t)-1;
            return;
        }
        cur->fd_table[fd].type = FD_SOCKET;
        cur->fd_table[fd].data = (void *)(uint64_t)sock_idx;
        cur->fd_table[fd].offset = 0;
        frame->rax = (uint64_t)fd;
        return;
    }

    case SYS_BIND: {
        /* sys_bind(fd, ip, port) */
        uint64_t fd = arg1;
        uint32_t ip = (uint32_t)arg2;
        uint16_t port = (uint16_t)arg3;
        struct process *cur = scheduler_get_current();
        if (fd >= PROCESS_MAX_FDS || cur->fd_table[fd].type != FD_SOCKET) {
            frame->rax = (uint64_t)-1;
            return;
        }
        int sock_idx = (int)(uint64_t)cur->fd_table[fd].data;
        frame->rax = (uint64_t)socket_bind(sock_idx, ip, port);
        return;
    }

    case SYS_LISTEN: {
        /* sys_listen(fd, backlog) */
        uint64_t fd = arg1;
        int backlog = (int)arg2;
        struct process *cur = scheduler_get_current();
        if (fd >= PROCESS_MAX_FDS || cur->fd_table[fd].type != FD_SOCKET) {
            frame->rax = (uint64_t)-1;
            return;
        }
        int sock_idx = (int)(uint64_t)cur->fd_table[fd].data;
        frame->rax = (uint64_t)socket_listen(sock_idx, backlog);
        return;
    }

    case SYS_ACCEPT: {
        /* sys_accept(fd) */
        uint64_t fd = arg1;
        struct process *cur = scheduler_get_current();
        if (fd >= PROCESS_MAX_FDS || cur->fd_table[fd].type != FD_SOCKET) {
            frame->rax = (uint64_t)-1;
            return;
        }
        int sock_idx = (int)(uint64_t)cur->fd_table[fd].data;
        int new_sock = socket_accept(sock_idx);
        if (new_sock < 0) {
            frame->rax = (uint64_t)-1;
            return;
        }
        int new_fd = process_fd_alloc(cur, 3);
        if (new_fd < 0) {
            socket_close(new_sock);
            frame->rax = (uint64_t)-1;
            return;
        }
        cur->fd_table[new_fd].type = FD_SOCKET;
        cur->fd_table[new_fd].data = (void *)(uint64_t)new_sock;
        cur->fd_table[new_fd].offset = 0;
        frame->rax = (uint64_t)new_fd;
        return;
    }

    case SYS_CONNECT: {
        /* sys_connect(fd, ip, port) */
        uint64_t fd = arg1;
        uint32_t ip = (uint32_t)arg2;
        uint16_t port = (uint16_t)arg3;
        struct process *cur = scheduler_get_current();
        if (fd >= PROCESS_MAX_FDS || cur->fd_table[fd].type != FD_SOCKET) {
            frame->rax = (uint64_t)-1;
            return;
        }
        int sock_idx = (int)(uint64_t)cur->fd_table[fd].data;
        frame->rax = (uint64_t)socket_connect(sock_idx, ip, port);
        return;
    }

    case SYS_SELECT: {
        /* sys_select(nfds, ptr to select_args) */
        uint32_t nfds = (uint32_t)arg1;
        uint8_t *args_ptr = (uint8_t *)arg2;

        if ((uint64_t)args_ptr >= KERNEL_VMA || nfds > PROCESS_MAX_FDS) {
            frame->rax = (uint64_t)-1;
            return;
        }

        /* select_args: uint32_t readfds, writefds, exceptfds; int32_t timeout_ms */
        uint32_t readfds, writefds, timeout_raw;
        memcpy(&readfds, args_ptr, 4);
        memcpy(&writefds, args_ptr + 4, 4);
        /* exceptfds at args_ptr + 8 — ignored */
        memcpy(&timeout_raw, args_ptr + 12, 4);
        int32_t timeout_ms = (int32_t)timeout_raw;

        struct process *cur = scheduler_get_current();
        uint32_t ready_read = 0, ready_write = 0;
        int max_polls = timeout_ms >= 0 ? (timeout_ms / 10 + 1) : 1000;

        for (int poll = 0; poll < max_polls; poll++) {
            ready_read = 0;
            ready_write = 0;

            for (uint32_t i = 0; i < nfds; i++) {
                if (readfds & (1U << i)) {
                    if (i >= PROCESS_MAX_FDS) continue;
                    int fdt = cur->fd_table[i].type;
                    bool readable = false;
                    if (fdt == FD_PIPE_READ) {
                        struct pipe *p = (struct pipe *)cur->fd_table[i].data;
                        readable = p && (p->count > 0 || p->writers == 0);
                    } else if (fdt == FD_CONSOLE) {
                        extern bool tty_readable(void);
                        readable = tty_readable();
                    } else if (fdt == FD_SOCKET) {
                        readable = socket_readable((int)(uint64_t)cur->fd_table[i].data);
                    }
                    if (readable)
                        ready_read |= (1U << i);
                }
                if (writefds & (1U << i)) {
                    if (i >= PROCESS_MAX_FDS) continue;
                    int fdt = cur->fd_table[i].type;
                    bool writable = false;
                    if (fdt == FD_PIPE_WRITE) {
                        struct pipe *p = (struct pipe *)cur->fd_table[i].data;
                        writable = p && (p->count < PIPE_BUF_SIZE);
                    } else if (fdt == FD_CONSOLE) {
                        writable = true;
                    } else if (fdt == FD_SOCKET) {
                        writable = socket_writable((int)(uint64_t)cur->fd_table[i].data);
                    }
                    if (writable)
                        ready_write |= (1U << i);
                }
            }

            if (ready_read || ready_write)
                break;

            if (timeout_ms == 0)
                break;

            /* Brief block — yield to scheduler */
            cur->state = PROCESS_BLOCKED;
            schedule();
            sti();
        }

        /* Write back results */
        memcpy(args_ptr, &ready_read, 4);
        memcpy(args_ptr + 4, &ready_write, 4);
        uint32_t zero = 0;
        memcpy(args_ptr + 8, &zero, 4);

        /* Count ready FDs */
        uint32_t count = 0;
        for (uint32_t i = 0; i < nfds; i++) {
            if (ready_read & (1U << i)) count++;
            if (ready_write & (1U << i)) count++;
        }
        frame->rax = (uint64_t)count;
        return;
    }

    case SYS_WAITPID: {
        /* sys_waitpid(pid, status_ptr, options) */
        int32_t wait_pid = (int32_t)arg1;
        int32_t *status_ptr = (int32_t *)arg2;
        /* arg3 = options, reserved for future use */

        struct process *cur = scheduler_get_current();

        /* Validate pointer if non-NULL */
        if (status_ptr && (uint64_t)status_ptr >= KERNEL_VMA) {
            frame->rax = (uint64_t)-1;
            return;
        }

        if (wait_pid > 0) {
            /* Wait for specific child */
            struct process *child = process_get_by_pid((uint32_t)wait_pid);
            if (!child || child->ppid != cur->pid) {
                frame->rax = (uint64_t)-1;
                return;
            }

            if (child->state == PROCESS_ZOMBIE) {
                if (status_ptr)
                    *status_ptr = child->exit_code;
                uint32_t zpid = child->pid;
                child->state = PROCESS_UNUSED;
                frame->rax = (uint64_t)zpid;
                return;
            }

            /* Block until this specific child exits */
            cur->wait_for_pid = (uint32_t)wait_pid;
            cur->state = PROCESS_BLOCKED;
            schedule();
            sti();

            /* Resumed — check again */
            child = process_get_by_pid((uint32_t)wait_pid);
            if (child && child->ppid == cur->pid &&
                child->state == PROCESS_ZOMBIE) {
                if (status_ptr)
                    *status_ptr = child->exit_code;
                uint32_t zpid = child->pid;
                child->state = PROCESS_UNUSED;
                frame->rax = (uint64_t)zpid;
                return;
            }

            frame->rax = (uint64_t)-1;
            return;
        }

        /* wait_pid <= 0: wait for any child (same as SYS_WAIT) */
        struct process *zombie = process_find_zombie_child(cur->pid);
        if (zombie) {
            uint32_t zpid = zombie->pid;
            if (status_ptr)
                *status_ptr = zombie->exit_code;
            zombie->state = PROCESS_UNUSED;
            frame->rax = (uint64_t)zpid;
            return;
        }

        if (!process_has_children(cur->pid)) {
            frame->rax = (uint64_t)-1;
            return;
        }

        cur->wait_for_pid = 0;
        cur->state = PROCESS_BLOCKED;
        schedule();
        sti();

        zombie = process_find_zombie_child(cur->pid);
        if (zombie) {
            uint32_t zpid = zombie->pid;
            if (status_ptr)
                *status_ptr = zombie->exit_code;
            zombie->state = PROCESS_UNUSED;
            frame->rax = (uint64_t)zpid;
            return;
        }

        frame->rax = (uint64_t)-1;
        return;
    }

    case 26: /* SYS_EXECV */
        syscall_execv(frame, arg1, arg2, arg3);
        return;

    case SYS_MMAP:
        frame->rax = syscall_mmap(arg1);
        return;

    case SYS_MUNMAP:
        frame->rax = syscall_munmap(arg1, arg2);
        return;

    case SYS_MPROTECT:
        frame->rax = syscall_mprotect(arg1, arg2, arg3);
        return;

    case SYS_BRK:
        frame->rax = syscall_brk(arg1);
        return;

    case SYS_EPOLL_CREATE:
        frame->rax = syscall_epoll_create();
        return;

    case SYS_EPOLL_CTL:
        frame->rax = syscall_epoll_ctl(arg1, arg2);
        return;

    case SYS_EPOLL_WAIT:
        frame->rax = syscall_epoll_wait(arg1, arg2);
        return;

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
