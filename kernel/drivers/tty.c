#include "tty.h"
#include "vga.h"
#include "../string.h"
#include "../signal/signal.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"

static struct tty console_tty;

void tty_init(void) {
    memset(&console_tty, 0, sizeof(console_tty));
    console_tty.flags = TTY_CANON | TTY_ECHO;
    console_tty.fg_pid = 0;
    debug_printf("TTY subsystem initialized\n");
}

void tty_input_char(char c) {
    /* Ctrl+C -> SIGINT */
    if (c == 3) {
        if (console_tty.fg_pid) {
            debug_printf("tty: Ctrl+C -> SIGINT to PID %u\n",
                         (uint64_t)console_tty.fg_pid);
            signal_send(console_tty.fg_pid, SIGINT);
        }
        return;
    }

    /* Ctrl+Z -> SIGTSTP */
    if (c == 26) {
        if (console_tty.fg_pid) {
            debug_printf("tty: Ctrl+Z -> SIGTSTP to PID %u\n",
                         (uint64_t)console_tty.fg_pid);
            signal_send(console_tty.fg_pid, SIGTSTP);
        }
        return;
    }

    /* Backspace */
    if (c == '\b') {
        if (console_tty.input_len > 0) {
            console_tty.input_len--;
            if (console_tty.flags & TTY_ECHO) {
                vga_putchar('\b');
            }
        }
        return;
    }

    /* Enter / newline */
    if (c == '\n') {
        if (console_tty.flags & TTY_ECHO) {
            vga_putchar('\n');
        }

        /* Copy input buffer to read buffer */
        memcpy(console_tty.read_buf, console_tty.input_buf,
               console_tty.input_len);
        console_tty.read_buf[console_tty.input_len] = '\n';
        console_tty.read_len = console_tty.input_len + 1;
        console_tty.read_pos = 0;
        console_tty.input_len = 0;

        /* Wake blocked reader */
        if (console_tty.blocked_reader) {
            struct process *reader = console_tty.blocked_reader;
            console_tty.blocked_reader = NULL;
            reader->state = PROCESS_READY;
            scheduler_add(reader);
        }
        return;
    }

    /* Printable characters */
    if (c >= ' ' && c <= '~') {
        if (console_tty.input_len < sizeof(console_tty.input_buf) - 1) {
            console_tty.input_buf[console_tty.input_len++] = c;
            if (console_tty.flags & TTY_ECHO) {
                vga_putchar(c);
            }
        }
    }
}

int tty_read(uint8_t *buf, uint32_t count) {
    /* Block until read_buf has data */
    while (console_tty.read_len == 0 ||
           console_tty.read_pos >= console_tty.read_len) {
        struct process *cur = scheduler_get_current();
        console_tty.blocked_reader = cur;
        cur->state = PROCESS_BLOCKED;
        schedule();
        sti();  /* Re-enable interrupts after being rescheduled */
    }

    /* Copy data from read buffer */
    uint32_t avail = console_tty.read_len - console_tty.read_pos;
    uint32_t to_copy = (count < avail) ? count : avail;
    memcpy(buf, console_tty.read_buf + console_tty.read_pos, to_copy);
    console_tty.read_pos += to_copy;

    /* Reset if fully consumed */
    if (console_tty.read_pos >= console_tty.read_len) {
        console_tty.read_pos = 0;
        console_tty.read_len = 0;
    }

    return (int)to_copy;
}

int tty_write(const uint8_t *buf, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        vga_putchar((char)buf[i]);
    }
    return (int)count;
}

void tty_set_fg(uint32_t pid) {
    console_tty.fg_pid = pid;
}

uint32_t tty_get_fg(void) {
    return console_tty.fg_pid;
}

bool tty_readable(void) {
    return console_tty.read_len > 0 && console_tty.read_pos < console_tty.read_len;
}
