#include "shell.h"
#include "../common.h"
#include "../string.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../drivers/pit.h"
#include "../memory/pmm.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"
#include "../process/user_process.h"
#include "../user_programs.h"
#include "../fs/vfs.h"
#include "../fs/ramfs.h"
#include "../fs/pipe.h"
#include "../fs/ext2.h"
#include "../signal/signal.h"
#include "../drivers/tty.h"
#include "../debug/debug.h"

#define CMD_BUFFER_SIZE 256
#define MAX_ARGS 16

/* Job tracking */
#define MAX_JOBS 8
struct job {
    uint32_t pid;
    char name[32];
    bool active;
};
static struct job jobs[MAX_JOBS];

/* Command function type */
typedef void (*cmd_func_t)(int argc, char **argv);

struct command {
    const char *name;
    const char *description;
    cmd_func_t func;
};

/* Forward declarations */
static void cmd_help(int argc, char **argv);
static void cmd_ps(int argc, char **argv);
static void cmd_mem(int argc, char **argv);
static void cmd_uptime(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_run(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_jobs(int argc, char **argv);
static void cmd_proc(int argc, char **argv);
static void cmd_fg(int argc, char **argv);
static void cmd_bg(int argc, char **argv);
static void cmd_kill(int argc, char **argv);
static void cmd_cat(int argc, char **argv);

static struct command commands[] = {
    {"help",   "Show available commands",     cmd_help},
    {"ps",     "List running processes",      cmd_ps},
    {"mem",    "Show memory statistics",      cmd_mem},
    {"uptime", "Show system uptime",          cmd_uptime},
    {"echo",   "Print arguments to screen",   cmd_echo},
    {"clear",  "Clear the screen",            cmd_clear},
    {"run",    "Run a program (& for bg)",    cmd_run},
    {"ls",     "List files in RAMFS",         cmd_ls},
    {"jobs",   "List active processes",       cmd_jobs},
    {"proc",   "Show process details",        cmd_proc},
    {"fg",     "Bring job to foreground",     cmd_fg},
    {"bg",     "Show background job status",  cmd_bg},
    {"kill",   "Send signal to process",      cmd_kill},
    {"cat",    "Print file contents",         cmd_cat},
    {NULL, NULL, NULL}
};

/* Simple string to uint conversion */
static uint32_t str_to_uint(const char *s) {
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return val;
}

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_printf("Available commands:\n");
    for (int i = 0; commands[i].name; i++) {
        vga_printf("  %-10s %s\n", commands[i].name, commands[i].description);
    }
}

static void cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    struct process *table = process_table_get();
    const char *state_names[] = {
        "unused", "ready", "running", "blocked", "zombie", "terminated"
    };

    vga_printf("PID  PPID STATE      USER NAME\n");
    vga_printf("---- ---- ---------- ---- ----------------\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].state != PROCESS_UNUSED &&
            table[i].state != PROCESS_TERMINATED) {
            vga_printf("%-4u %-4u %-10s %-4s %s\n",
                       (uint64_t)table[i].pid,
                       (uint64_t)table[i].ppid,
                       state_names[table[i].state],
                       table[i].is_user ? "yes" : "no",
                       table[i].name);
        }
    }
}

static void cmd_mem(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t free = pmm_get_free_pages();
    uint64_t total = pmm_get_total_pages();
    uint64_t used = total - free;

    vga_printf("Memory Statistics:\n");
    vga_printf("  Total:  %u pages (%u MB)\n", total, total * 4 / 1024);
    vga_printf("  Used:   %u pages (%u MB)\n", used, used * 4 / 1024);
    vga_printf("  Free:   %u pages (%u MB)\n", free, free * 4 / 1024);
}

static void cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t ticks = pit_get_ticks();
    uint64_t secs = pit_get_uptime_seconds();
    uint64_t mins = secs / 60;
    secs %= 60;
    vga_printf("Uptime: %u min %u sec (%u ticks)\n", mins, secs, ticks);
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) vga_putchar(' ');
        vga_puts(argv[i]);
    }
    vga_putchar('\n');
}

static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_clear();
}

/* Add a job to the job table, return job ID (1-based) */
static int job_add(uint32_t pid, const char *name) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            jobs[i].pid = pid;
            strncpy(jobs[i].name, name, sizeof(jobs[i].name) - 1);
            jobs[i].name[sizeof(jobs[i].name) - 1] = '\0';
            jobs[i].active = true;
            return i + 1;
        }
    }
    return -1;
}

/* Check for completed background jobs and print notifications */
static void check_bg_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            struct process *p = process_get_by_pid(jobs[i].pid);
            if (!p || p->state == PROCESS_ZOMBIE ||
                p->state == PROCESS_TERMINATED ||
                p->state == PROCESS_UNUSED) {
                vga_printf("[%u] Done  %s\n", (uint64_t)(i + 1), jobs[i].name);
                /* Reap if zombie */
                if (p && p->state == PROCESS_ZOMBIE)
                    p->state = PROCESS_TERMINATED;
                jobs[i].active = false;
            }
        }
    }
}

static void cmd_run(int argc, char **argv) {
    if (argc < 2) {
        vga_printf("Usage: run <program> [&]\n");
        vga_printf("Use 'ls' to list available programs.\n");
        return;
    }

    /* Check for trailing & (background) */
    bool background = false;
    if (argc >= 3 && strcmp(argv[argc - 1], "&") == 0) {
        background = true;
    }

    uint64_t prog_size = 0;
    const uint8_t *prog_data = ramfs_get_file_data(argv[1], &prog_size);
    if (!prog_data) {
        vga_printf("Program not found: %s\n", argv[1]);
        return;
    }

    /* Build argument string from argv[2..] (excluding trailing &) */
    char arg_str[256];
    arg_str[0] = '\0';
    int arg_end = background ? argc - 1 : argc;
    for (int i = 2; i < arg_end; i++) {
        if (i > 2) {
            size_t cur_len = strlen(arg_str);
            if (cur_len < sizeof(arg_str) - 1) {
                arg_str[cur_len] = ' ';
                arg_str[cur_len + 1] = '\0';
            }
        }
        size_t cur_len = strlen(arg_str);
        strncpy(arg_str + cur_len, argv[i], sizeof(arg_str) - cur_len - 1);
        arg_str[sizeof(arg_str) - 1] = '\0';
    }

    struct process *shell = scheduler_get_current();
    const char *args_ptr = arg_str[0] ? arg_str : NULL;
    if (user_process_create_args(argv[1], prog_data, prog_size,
                                  shell->pid, args_ptr) < 0) {
        vga_printf("Failed to create user process\n");
        return;
    }

    if (background) {
        /* Find the just-created process (highest PID child of shell) */
        struct process *table = process_table_get();
        uint32_t child_pid = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (table[i].ppid == shell->pid &&
                table[i].state != PROCESS_UNUSED &&
                table[i].state != PROCESS_TERMINATED &&
                table[i].pid > child_pid) {
                child_pid = table[i].pid;
            }
        }
        int job_id = job_add(child_pid, argv[1]);
        vga_printf("[%u] %u\n", (uint64_t)job_id, (uint64_t)child_pid);
    } else {
        /* Foreground: set TTY fg, block until child exits */
        struct process *table2 = process_table_get();
        uint32_t child_pid2 = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (table2[i].ppid == shell->pid &&
                table2[i].state != PROCESS_UNUSED &&
                table2[i].state != PROCESS_TERMINATED &&
                table2[i].pid > child_pid2) {
                child_pid2 = table2[i].pid;
            }
        }
        tty_set_fg(child_pid2);

        int32_t status = 0;
        int pid = process_wait_for(0, &status);
        tty_set_fg(0);
        if (pid > 0) {
            vga_printf("Process %u exited with status %d\n",
                       (uint64_t)pid, (int64_t)status);
        }
    }
}

static void cmd_ls(int argc, char **argv) {
    const char *path = "/";
    if (argc >= 2)
        path = argv[1];

    /* Check for mount point (e.g., /dev, /proc) */
    struct vfs_ops *mount_ops = vfs_get_mount_ops(path);
    if (mount_ops && mount_ops->readdir) {
        struct vfs_node tmp;
        memset(&tmp, 0, sizeof(tmp));
        strncpy(tmp.name, path, VFS_MAX_NAME - 1);
        tmp.ops = mount_ops;

        char name[VFS_MAX_NAME];
        uint32_t index = 0;
        while (mount_ops->readdir(&tmp, index, name, sizeof(name)) == 0) {
            vga_printf("  %s\n", name);
            index++;
        }
        if (index == 0)
            vga_printf("  (empty)\n");
        return;
    }

    /* For non-root paths, try ext2 first */
    if (strcmp(path, "/") != 0 && ext2_is_mounted()) {
        uint32_t ino = ext2_path_lookup(path);
        if (ino) {
            struct ext2_inode inode;
            if (ext2_read_inode(ino, &inode) == 0 &&
                (inode.i_mode & EXT2_S_IFDIR)) {
                uint8_t dir_buf[4096];
                uint32_t dir_size = inode.i_size;
                if (dir_size > sizeof(dir_buf))
                    dir_size = sizeof(dir_buf);
                int rd = ext2_read_file(&inode, 0, dir_size, dir_buf);
                if (rd > 0) {
                    uint32_t pos = 0;
                    uint32_t count = 0;
                    while (pos < (uint32_t)rd) {
                        struct ext2_dir_entry *de =
                            (struct ext2_dir_entry *)(dir_buf + pos);
                        if (de->rec_len == 0)
                            break;
                        if (de->inode != 0 && de->name_len > 0) {
                            char dname[256];
                            uint8_t nlen = de->name_len;
                            memcpy(dname, dir_buf + pos +
                                   sizeof(struct ext2_dir_entry), nlen);
                            dname[nlen] = '\0';
                            vga_printf("  %s\n", dname);
                            count++;
                        }
                        pos += de->rec_len;
                    }
                    if (count == 0)
                        vga_printf("  (empty)\n");
                    return;
                }
            }
        }
    }

    /* Root or VFS: show RAMFS listing */
    char name[VFS_MAX_NAME];
    uint32_t index = 0;

    while (vfs_readdir(path, index, name, sizeof(name)) == 0) {
        vga_printf("  %s\n", name);
        index++;
    }

    /* For root, also show ext2 root entries (skip . and ..) */
    if (strcmp(path, "/") == 0 && ext2_is_mounted()) {
        uint32_t ino = ext2_path_lookup("/");
        if (ino) {
            struct ext2_inode inode;
            if (ext2_read_inode(ino, &inode) == 0 &&
                (inode.i_mode & EXT2_S_IFDIR)) {
                uint8_t dir_buf[4096];
                uint32_t dir_size = inode.i_size;
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
                            char dname[256];
                            uint8_t nlen = de->name_len;
                            memcpy(dname, dir_buf + pos +
                                   sizeof(struct ext2_dir_entry), nlen);
                            dname[nlen] = '\0';
                            /* Skip . and .. */
                            if (!(nlen == 1 && dname[0] == '.') &&
                                !(nlen == 2 && dname[0] == '.' && dname[1] == '.')) {
                                vga_printf("  %s\n", dname);
                                index++;
                            }
                        }
                        pos += de->rec_len;
                    }
                }
            }
        }
    }

    if (index == 0)
        vga_printf("  (empty)\n");
}

static void cmd_jobs(int argc, char **argv) {
    (void)argc; (void)argv;
    int count = 0;

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            struct process *p = process_get_by_pid(jobs[i].pid);
            const char *st = "unknown";
            if (p) {
                const char *state_names[] = {
                    "unused", "ready", "running", "blocked",
                    "zombie", "terminated"
                };
                st = state_names[p->state];
            }
            vga_printf("  [%u] PID=%-4u %-10s %s\n",
                       (uint64_t)(i + 1),
                       (uint64_t)jobs[i].pid, st, jobs[i].name);
            count++;
        }
    }

    if (count == 0)
        vga_printf("No background jobs.\n");
}

static void cmd_proc(int argc, char **argv) {
    if (argc < 2) {
        vga_printf("Usage: proc <pid>\n");
        return;
    }

    uint32_t pid = str_to_uint(argv[1]);
    if (pid == 0) {
        vga_printf("Invalid PID: %s\n", argv[1]);
        return;
    }

    struct process *proc = process_get_by_pid(pid);
    if (!proc) {
        vga_printf("Process %u not found.\n", (uint64_t)pid);
        return;
    }

    const char *state_names[] = {
        "unused", "ready", "running", "blocked", "zombie", "terminated"
    };

    vga_printf("PID:   %u\n", (uint64_t)proc->pid);
    vga_printf("PPID:  %u\n", (uint64_t)proc->ppid);
    vga_printf("Name:  %s\n", proc->name);
    vga_printf("State: %s\n", state_names[proc->state]);
    vga_printf("User:  %s\n", proc->is_user ? "yes" : "no");
    vga_printf("CR3:   0x%x\n", proc->cr3);

    /* Show open FDs */
    vga_printf("FDs:   ");
    const char *type_names[] = {"none", "vfs", "pipe_r", "pipe_w", "console"};
    for (int i = 0; i < PROCESS_MAX_FDS; i++) {
        if (proc->fd_table[i].type != FD_NONE) {
            vga_printf("%u:%s ", (uint64_t)i, type_names[proc->fd_table[i].type]);
        }
    }
    vga_putchar('\n');
}

static void cmd_fg(int argc, char **argv) {
    if (argc < 2) {
        vga_printf("Usage: fg <job_id>\n");
        return;
    }

    uint32_t job_id = str_to_uint(argv[1]);
    if (job_id < 1 || job_id > MAX_JOBS || !jobs[job_id - 1].active) {
        vga_printf("No such job: %u\n", (uint64_t)job_id);
        return;
    }

    struct job *j = &jobs[job_id - 1];
    vga_printf("Bringing '%s' (PID %u) to foreground\n",
               j->name, (uint64_t)j->pid);

    tty_set_fg(j->pid);
    int32_t status = 0;
    int pid = process_wait_for(j->pid, &status);
    tty_set_fg(0);
    if (pid > 0) {
        vga_printf("Process %u exited with status %d\n",
                   (uint64_t)pid, (int64_t)status);
    }
    j->active = false;
}

static void cmd_bg(int argc, char **argv) {
    if (argc < 2) {
        vga_printf("Usage: bg <job_id>\n");
        return;
    }

    uint32_t job_id = str_to_uint(argv[1]);
    if (job_id < 1 || job_id > MAX_JOBS || !jobs[job_id - 1].active) {
        vga_printf("No such job: %u\n", (uint64_t)job_id);
        return;
    }

    vga_printf("[%u] Running  %s (PID %u)\n",
               (uint64_t)job_id, jobs[job_id - 1].name,
               (uint64_t)jobs[job_id - 1].pid);
}

static void cmd_kill(int argc, char **argv) {
    if (argc < 2) {
        vga_printf("Usage: kill <pid> [signal]\n");
        return;
    }

    uint32_t pid = str_to_uint(argv[1]);
    int sig = 15;  /* SIGTERM by default */
    if (argc >= 3)
        sig = (int)str_to_uint(argv[2]);

    struct process *target = process_get_by_pid(pid);
    if (!target) {
        vga_printf("No such process: %u\n", (uint64_t)pid);
        return;
    }

    signal_send(pid, sig);
    vga_printf("Sent signal %d to PID %u\n", (int64_t)sig, (uint64_t)pid);
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        vga_printf("Usage: cat <file>\n");
        return;
    }

    const char *path = argv[1];

    /* Check if it's a /proc path */
    struct vfs_ops *mount_ops = vfs_get_mount_ops(path);
    if (mount_ops) {
        /* Create a temporary node for procfs */
        struct vfs_node tmp_node;
        memset(&tmp_node, 0, sizeof(tmp_node));
        strncpy(tmp_node.name, path, VFS_MAX_NAME - 1);
        tmp_node.ops = mount_ops;
        tmp_node.in_use = true;

        uint8_t buf[512];
        int bytes = mount_ops->read(&tmp_node, 0, sizeof(buf) - 1, buf);
        if (bytes > 0) {
            buf[bytes] = 0;
            vga_printf("%s", (char *)buf);
        } else {
            vga_printf("(empty or error)\n");
        }
        return;
    }

    /* Regular VFS file */
    int fd = vfs_open(path);
    if (fd < 0) {
        vga_printf("File not found: %s\n", path);
        return;
    }

    uint8_t buf[256];
    int bytes;
    while ((bytes = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = 0;
        vga_printf("%s", (char *)buf);
    }

    vfs_close(fd);
}

static void shell_readline(char *buf, size_t size) {
    /* TTY handles echo, backspace, and line buffering.
     * tty_read blocks until a complete line is ready. */
    uint8_t tmp[256];
    int n = tty_read(tmp, (uint32_t)(size - 1));
    if (n <= 0) {
        buf[0] = '\0';
        return;
    }
    /* Strip trailing newline */
    if (n > 0 && tmp[n - 1] == '\n')
        n--;
    memcpy(buf, tmp, (size_t)n);
    buf[n] = '\0';
}

/* Execute a single command (no pipe) */
static void shell_exec_single(char *cmd) {
    char *argv[MAX_ARGS];
    int argc = 0;

    char *token = strtok(cmd, " \t");
    while (token && argc < MAX_ARGS) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }

    if (argc == 0)
        return;

    for (int i = 0; commands[i].name; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }

    vga_printf("Unknown command: %s\n", argv[0]);
    vga_printf("Type 'help' for available commands.\n");
}

/* Check if a command line contains a pipe */
static char *find_pipe(char *line) {
    for (char *p = line; *p; p++) {
        if (*p == '|')
            return p;
    }
    return NULL;
}

static void shell_execute(char *line) {
    /* Check for pipe operator */
    char *pipe_pos = find_pipe(line);

    if (!pipe_pos) {
        shell_exec_single(line);
        return;
    }

    /* Split at pipe: "cmd1 | cmd2" */
    *pipe_pos = '\0';
    char *cmd1 = line;
    char *cmd2 = pipe_pos + 1;

    /* Skip whitespace */
    while (*cmd2 == ' ' || *cmd2 == '\t') cmd2++;

    /* Parse cmd1 to get the program name */
    char cmd1_copy[CMD_BUFFER_SIZE];
    strncpy(cmd1_copy, cmd1, CMD_BUFFER_SIZE - 1);
    cmd1_copy[CMD_BUFFER_SIZE - 1] = '\0';
    char *prog1 = strtok(cmd1_copy, " \t");

    char cmd2_copy[CMD_BUFFER_SIZE];
    strncpy(cmd2_copy, cmd2, CMD_BUFFER_SIZE - 1);
    cmd2_copy[CMD_BUFFER_SIZE - 1] = '\0';
    char *prog2 = strtok(cmd2_copy, " \t");

    if (!prog1 || !prog2) {
        vga_printf("Invalid pipe syntax\n");
        return;
    }

    /* Both sides must be user programs */
    uint64_t size1 = 0, size2 = 0;
    const uint8_t *data1 = ramfs_get_file_data(prog1, &size1);
    const uint8_t *data2 = ramfs_get_file_data(prog2, &size2);

    if (!data1) {
        vga_printf("Program not found: %s\n", prog1);
        return;
    }
    if (!data2) {
        vga_printf("Program not found: %s\n", prog2);
        return;
    }

    /* Create pipe */
    struct pipe *p = NULL;
    if (pipe_create(&p) < 0) {
        vga_printf("Failed to create pipe\n");
        return;
    }

    struct process *shell = scheduler_get_current();

    /* Create child1: stdout -> pipe write end */
    if (user_process_create(prog1, data1, size1, shell->pid) < 0) {
        vga_printf("Failed to create %s\n", prog1);
        return;
    }
    /* Find the child1 process (most recent child) */
    struct process *table = process_table_get();
    struct process *child1 = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].ppid == shell->pid &&
            table[i].state != PROCESS_UNUSED &&
            table[i].state != PROCESS_TERMINATED &&
            strcmp(table[i].name, prog1) == 0) {
            if (!child1 || table[i].pid > child1->pid)
                child1 = &table[i];
        }
    }
    if (child1) {
        /* Redirect stdout to pipe write end */
        child1->fd_table[1].type = FD_PIPE_WRITE;
        child1->fd_table[1].data = p;
        p->writers++;  /* Extra writer for child1's fd 1 */
    }

    /* Create child2: stdin -> pipe read end */
    if (user_process_create(prog2, data2, size2, shell->pid) < 0) {
        vga_printf("Failed to create %s\n", prog2);
        return;
    }
    struct process *child2 = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].ppid == shell->pid &&
            table[i].state != PROCESS_UNUSED &&
            table[i].state != PROCESS_TERMINATED &&
            strcmp(table[i].name, prog2) == 0) {
            if (!child2 || table[i].pid > child2->pid)
                child2 = &table[i];
        }
    }
    if (child2) {
        /* Redirect stdin to pipe read end */
        child2->fd_table[0].type = FD_PIPE_READ;
        child2->fd_table[0].data = p;
        p->readers++;  /* Extra reader for child2's fd 0 */
    }

    /* Wait for both children */
    int32_t status = 0;
    process_wait_for(0, &status);
    process_wait_for(0, &status);
}

void shell_run(void) {
    char line[CMD_BUFFER_SIZE];

    /* When first scheduled via context_switch inside the PIT ISR,
     * interrupts are disabled (CPU clears IF on interrupt entry).
     * We must re-enable them or hlt() will halt forever. */
    sti();

    debug_printf("shell: shell_run started\n");

    for (;;) {
        /* Check for completed background jobs */
        check_bg_jobs();

        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("hobbyos");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_puts("> ");

        shell_readline(line, sizeof(line));
        debug_printf("shell> %s\n", line);
        shell_execute(line);
    }
}
