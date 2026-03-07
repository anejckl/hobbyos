#include "procfs.h"
#include "vfs.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"
#include "../string.h"
#include "../debug/debug.h"

/* Simple integer to string helper */
static int int_to_str(char *buf, int64_t val) {
    int i = 0;
    bool neg = false;
    if (val < 0) {
        neg = true;
        val = -val;
    }
    if (val == 0) {
        buf[i++] = '0';
    } else {
        char tmp[20];
        int j = 0;
        while (val > 0) {
            tmp[j++] = '0' + (int)(val % 10);
            val /= 10;
        }
        if (neg)
            buf[i++] = '-';
        while (j > 0)
            buf[i++] = tmp[--j];
    }
    buf[i] = '\0';
    return i;
}

/* Parse PID from a procfs path like "/proc/42/status" */
static uint32_t parse_pid_from_path(const char *path) {
    /* path is like "/proc/42/status" or "proc/42/status" */
    const char *p = path;
    if (*p == '/') p++;
    if (strncmp(p, "proc/", 5) != 0)
        return 0;
    p += 5;

    /* Handle "self" */
    if (strncmp(p, "self", 4) == 0 && (p[4] == '/' || p[4] == '\0')) {
        struct process *cur = scheduler_get_current();
        return cur ? cur->pid : 0;
    }

    /* Parse numeric PID */
    uint32_t pid = 0;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (uint32_t)(*p - '0');
        p++;
    }
    return pid;
}

/* Get the file type from path (after /proc/<pid>/) */
static const char *get_procfs_file(const char *path) {
    const char *p = path;
    if (*p == '/') p++;
    if (strncmp(p, "proc/", 5) != 0)
        return NULL;
    p += 5;

    /* Skip PID or "self" */
    while (*p && *p != '/')
        p++;
    if (*p == '/')
        p++;
    return p;  /* "status", "fd", etc. */
}

/* Generate /proc/<pid>/status content */
static int procfs_gen_status(uint32_t pid, uint8_t *buf, uint64_t offset,
                             uint64_t size) {
    struct process *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;

    const char *state_names[] = {
        "UNUSED", "READY", "RUNNING", "BLOCKED", "ZOMBIE", "TERMINATED"
    };

    /* Build content into a temporary buffer */
    char tmp[512];
    int len = 0;
    char numbuf[20];

    /* PID: N */
    memcpy(tmp + len, "PID: ", 5); len += 5;
    len += int_to_str(tmp + len, (int64_t)proc->pid);
    tmp[len++] = '\n';

    /* PPID: N */
    memcpy(tmp + len, "PPID: ", 6); len += 6;
    len += int_to_str(tmp + len, (int64_t)proc->ppid);
    tmp[len++] = '\n';

    /* STATE: X */
    memcpy(tmp + len, "STATE: ", 7); len += 7;
    const char *st = state_names[proc->state];
    int sl = (int)strlen(st);
    memcpy(tmp + len, st, (size_t)sl); len += sl;
    tmp[len++] = '\n';

    /* NAME: X */
    memcpy(tmp + len, "NAME: ", 6); len += 6;
    sl = (int)strlen(proc->name);
    memcpy(tmp + len, proc->name, (size_t)sl); len += sl;
    tmp[len++] = '\n';

    /* USER: yes/no */
    memcpy(tmp + len, "USER: ", 6); len += 6;
    if (proc->is_user) {
        memcpy(tmp + len, "yes", 3); len += 3;
    } else {
        memcpy(tmp + len, "no", 2); len += 2;
    }
    tmp[len++] = '\n';

    (void)numbuf;

    /* Copy requested portion */
    if ((int64_t)offset >= len)
        return 0;
    int avail = len - (int)offset;
    int to_copy = (int)size < avail ? (int)size : avail;
    memcpy(buf, tmp + offset, (size_t)to_copy);
    return to_copy;
}

/* Generate /proc/<pid>/fd content */
static int procfs_gen_fd(uint32_t pid, uint8_t *buf, uint64_t offset,
                         uint64_t size) {
    struct process *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;

    const char *type_names[] = {
        "none", "vfs", "pipe_r", "pipe_w", "console"
    };

    char tmp[256];
    int len = 0;

    for (int i = 0; i < PROCESS_MAX_FDS; i++) {
        if (proc->fd_table[i].type != FD_NONE) {
            len += int_to_str(tmp + len, (int64_t)i);
            memcpy(tmp + len, ": ", 2); len += 2;
            int t = proc->fd_table[i].type;
            if (t >= 0 && t <= 4) {
                int tl = (int)strlen(type_names[t]);
                memcpy(tmp + len, type_names[t], (size_t)tl);
                len += tl;
            }
            tmp[len++] = '\n';
            if (len > 240) break;
        }
    }

    if ((int64_t)offset >= len)
        return 0;
    int avail = len - (int)offset;
    int to_copy = (int)size < avail ? (int)size : avail;
    memcpy(buf, tmp + offset, (size_t)to_copy);
    return to_copy;
}

/* Procfs read handler — dispatches based on path stored in node name */
static int procfs_read(struct vfs_node *node, uint64_t offset, uint64_t size,
                       uint8_t *buffer) {
    if (!node || !buffer)
        return -1;

    uint32_t pid = parse_pid_from_path(node->name);
    if (pid == 0)
        return -1;

    const char *file = get_procfs_file(node->name);
    if (!file)
        return -1;

    if (strcmp(file, "status") == 0)
        return procfs_gen_status(pid, buffer, offset, size);
    else if (strcmp(file, "fd") == 0)
        return procfs_gen_fd(pid, buffer, offset, size);

    return -1;
}

static struct vfs_ops procfs_ops = {
    .read = procfs_read,
    .write = NULL,
    .readdir = NULL,
};

void procfs_init(void) {
    vfs_mount("/proc", &procfs_ops);
    debug_printf("procfs: mounted at /proc\n");
}
