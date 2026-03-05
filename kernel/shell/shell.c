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

static struct command commands[] = {
    {"help",   "Show available commands",     cmd_help},
    {"ps",     "List running processes",      cmd_ps},
    {"mem",    "Show memory statistics",      cmd_mem},
    {"uptime", "Show system uptime",          cmd_uptime},
    {"echo",   "Print arguments to screen",   cmd_echo},
    {"clear",  "Clear the screen",            cmd_clear},
    {"run",    "Run a user program",          cmd_run},
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
    const char *state_names[] = {"unused", "ready", "running", "blocked", "terminated"};

    vga_printf("PID  STATE      NAME\n");
    vga_printf("---- ---------- ----------------\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].state != PROCESS_UNUSED) {
            vga_printf("%-4u %-10s %s\n",
                       (uint64_t)table[i].pid,
                       state_names[table[i].state],
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
        vga_printf("Usage: run <program>\n");
        vga_printf("Available: hello, counter\n");
        return;
    }

    const struct user_program *prog = user_programs_find(argv[1]);
    if (!prog) {
        vga_printf("Unknown program: %s\n", argv[1]);
        vga_printf("Available: hello, counter\n");
        return;
    }

    if (user_process_create(argv[1], prog->data, prog->size) < 0) {
        vga_printf("Failed to create user process\n");
        return;
    }

    vga_printf("Started '%s' as user process\n", argv[1]);
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
