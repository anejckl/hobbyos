#include "poll.h"
#include "../process/process.h"
#include "pipe.h"
#include "../drivers/tty.h"
#include "../net/socket.h"

bool fd_is_readable(struct process *proc, int fd) {
    if (fd < 0 || fd >= PROCESS_MAX_FDS)
        return false;
    int fdt = proc->fd_table[fd].type;
    if (fdt == FD_PIPE_READ) {
        struct pipe *p = (struct pipe *)proc->fd_table[fd].data;
        return p && (p->count > 0 || p->writers == 0);
    } else if (fdt == FD_CONSOLE) {
        return tty_readable();
    } else if (fdt == FD_SOCKET) {
        return socket_readable((int)(uint64_t)proc->fd_table[fd].data);
    } else if (fdt == FD_EXT2 || fdt == FD_VFS) {
        return true;  /* files are always readable */
    }
    return false;
}

bool fd_is_writable(struct process *proc, int fd) {
    if (fd < 0 || fd >= PROCESS_MAX_FDS)
        return false;
    int fdt = proc->fd_table[fd].type;
    if (fdt == FD_PIPE_WRITE) {
        struct pipe *p = (struct pipe *)proc->fd_table[fd].data;
        return p && (p->count < PIPE_BUF_SIZE);
    } else if (fdt == FD_CONSOLE) {
        return true;
    } else if (fdt == FD_SOCKET) {
        return socket_writable((int)(uint64_t)proc->fd_table[fd].data);
    } else if (fdt == FD_EXT2 || fdt == FD_VFS) {
        return true;
    }
    return false;
}
