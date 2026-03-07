#include "syscall.h"
#include "../arch/x86_64/isr.h"
#include "../arch/x86_64/fork_return.h"
#include "../drivers/vga.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../process/user_process.h"
#include "../memory/user_vm.h"
#include "../memory/kheap.h"
#include "../fs/vfs.h"
#include "../fs/ramfs.h"
#include "../fs/pipe.h"
#include "../fs/ext2.h"
#include "../signal/signal.h"
#include "../drivers/tty.h"
#include "../drivers/device.h"
#include "../memory/user_access.h"
#include "../string.h"
#include "../debug/debug.h"
#include "../net/socket.h"
#include "../net/tcp.h"

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

    case SYS_EXIT: {
        /* sys_exit(status) */
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

        /* Try ext2 filesystem first */
        uint64_t prog_size = 0;
        const uint8_t *prog_data = NULL;
        uint8_t *ext2_buf = NULL;

        if (ext2_is_mounted()) {
            /* Try /bin/<name> on ext2 */
            char ext2_path[64];
            ext2_path[0] = '/';
            ext2_path[1] = 'b';
            ext2_path[2] = 'i';
            ext2_path[3] = 'n';
            ext2_path[4] = '/';
            strncpy(ext2_path + 5, path, sizeof(ext2_path) - 6);
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
            prog_data = ramfs_get_file_data(path, &prog_size);

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
                /* Pipe refcounts handled by pipe_create's initial setup:
                 * readers/writers count tracks open ends. Increment on fork. */
                struct pipe *p = (struct pipe *)child->fd_table[i].data;
                if (p) {
                    /* Access pipe fields directly (defined in pipe.h) */
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
                        uint8_t dir_buf[4096];
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
                uint8_t dir_buf[4096];
                if (dir_size > sizeof(dir_buf))
                    dir_size = sizeof(dir_buf);
                int rd = ext2_read_file(&inode, 0, dir_size, dir_buf);
                if (rd > 0) {
                    uint32_t pos = 0;
                    while (pos < (uint32_t)rd) {
                        struct ext2_dir_entry *de =
                            (struct ext2_dir_entry *)(dir_buf + pos);
                        if (de->rec_len == 0)
                            break;
                        if (de->inode != 0 && de->name_len > 0) {
                            char *de_name = (char *)(dir_buf + pos +
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
