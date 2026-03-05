#ifndef DEBUG_H
#define DEBUG_H

#include "../common.h"

/* COM1 serial port */
#define COM1_PORT 0x3F8

void debug_init(void);
void debug_putchar(char c);
void debug_puts(const char *s);
void debug_printf(const char *fmt, ...);

#endif /* DEBUG_H */
