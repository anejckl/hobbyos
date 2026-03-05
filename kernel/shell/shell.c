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
#include "../debug/debug.h"

#define CMD_BUFFER_SIZE 256
#define MAX_ARGS 16

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
    {NULL, NULL, NULL}
};

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

    struct process *shell = scheduler_get_current();
    if (user_process_create(argv[1], prog_data, prog_size, shell->pid) < 0) {
        vga_printf("Failed to create user process\n");
        return;
    }

    if (background) {
        vga_printf("Started '%s' in background\n", argv[1]);
    } else {
        /* Foreground: block until child exits */
        int32_t status = 0;
        int pid = process_wait_for(0, &status);
        if (pid > 0) {
            vga_printf("Process %u exited with status %d\n",
                       (uint64_t)pid, (int64_t)status);
        }
    }
}

static void cmd_ls(int argc, char **argv) {
    (void)argc; (void)argv;
    char name[VFS_MAX_NAME];
    uint32_t index = 0;

    while (vfs_readdir("/", index, name, sizeof(name)) == 0) {
        vga_printf("  %s\n", name);
        index++;
    }

    if (index == 0)
        vga_printf("  (empty)\n");
}

static void cmd_jobs(int argc, char **argv) {
    (void)argc; (void)argv;
    struct process *table = process_table_get();
    const char *state_names[] = {
        "unused", "ready", "running", "blocked", "zombie", "terminated"
    };
    int count = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].is_user &&
            table[i].state != PROCESS_UNUSED &&
            table[i].state != PROCESS_TERMINATED) {
            vga_printf("  [%u] %-10s %s\n",
                       (uint64_t)table[i].pid,
                       state_names[table[i].state],
                       table[i].name);
            count++;
        }
    }

    if (count == 0)
        vga_printf("No active user processes.\n");
}

static void cmd_proc(int argc, char **argv) {
    if (argc < 2) {
        vga_printf("Usage: proc <pid>\n");
        return;
    }

    /* Simple string-to-uint conversion */
    uint32_t pid = 0;
    for (int i = 0; argv[1][i]; i++) {
        if (argv[1][i] < '0' || argv[1][i] > '9') {
            vga_printf("Invalid PID: %s\n", argv[1]);
            return;
        }
        pid = pid * 10 + (uint32_t)(argv[1][i] - '0');
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
}

static void shell_readline(char *buf, size_t size) {
    size_t pos = 0;
    while (pos < size - 1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            vga_putchar('\n');
            break;
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                vga_putchar('\b');
            }
        } else if (c >= ' ' && c <= '~') {
            buf[pos++] = c;
            vga_putchar(c);
        }
    }
    buf[pos] = '\0';
}

static void shell_execute(char *line) {
    char *argv[MAX_ARGS];
    int argc = 0;

    /* Tokenize input */
    char *token = strtok(line, " \t");
    while (token && argc < MAX_ARGS) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }

    if (argc == 0)
        return;

    /* Look up command */
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }

    vga_printf("Unknown command: %s\n", argv[0]);
    vga_printf("Type 'help' for available commands.\n");
}

void shell_run(void) {
    char line[CMD_BUFFER_SIZE];

    /* When first scheduled via context_switch inside the PIT ISR,
     * interrupts are disabled (CPU clears IF on interrupt entry).
     * We must re-enable them or hlt() will halt forever. */
    sti();

    debug_printf("shell: shell_run started\n");

    for (;;) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("hobbyos");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_puts("> ");

        shell_readline(line, sizeof(line));
        debug_printf("shell> %s\n", line);
        shell_execute(line);
    }
}
