#ifndef TTY_H
#define TTY_H

#include "../common.h"

/* TTY flags */
#define TTY_CANON  (1 << 0)   /* Canonical (line-buffered) mode */
#define TTY_ECHO   (1 << 1)   /* Echo input characters */

struct tty {
    char input_buf[256];       /* Line editing buffer (canonical mode) */
    uint32_t input_len;
    char read_buf[256];        /* Completed line ready to be read */
    uint32_t read_pos, read_len;
    uint32_t flags;            /* TTY_CANON | TTY_ECHO */
    uint32_t fg_pid;           /* Foreground process PID */
    struct process *blocked_reader;
};

/* Initialize the TTY subsystem */
void tty_init(void);

/* Called from keyboard IRQ handler for each character */
void tty_input_char(char c);

/* Read from TTY (blocks until data available in canonical mode) */
int tty_read(uint8_t *buf, uint32_t count);

/* Write to TTY (VGA + serial output) */
int tty_write(const uint8_t *buf, uint32_t count);

/* Set/get the foreground process PID */
void tty_set_fg(uint32_t pid);
uint32_t tty_get_fg(void);

#endif /* TTY_H */
