#include "debug.h"
#include "../string.h"

bool debug_serial_ready = false;

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

void debug_init(void) {
    /* Initialize COM1 at 38400 baud */
    outb(COM1_PORT + 1, 0x00);    /* Disable interrupts */
    outb(COM1_PORT + 3, 0x80);    /* Enable DLAB */
    outb(COM1_PORT + 0, 0x03);    /* Divisor low byte (38400 baud) */
    outb(COM1_PORT + 1, 0x00);    /* Divisor high byte */
    outb(COM1_PORT + 3, 0x03);    /* 8 bits, no parity, 1 stop bit */
    outb(COM1_PORT + 2, 0xC7);    /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1_PORT + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */
    debug_serial_ready = true;
}

static int serial_is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void debug_putchar(char c) {
    while (!serial_is_transmit_empty())
        ;
    outb(COM1_PORT, c);
}

void debug_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            debug_putchar('\r');
        debug_putchar(*s++);
    }
}

static void debug_print_uint(uint64_t val, int base) {
    char buf[24];
    int i = 0;
    const char *digits = "0123456789abcdef";

    if (val == 0) {
        debug_putchar('0');
        return;
    }

    while (val > 0) {
        buf[i++] = digits[val % base];
        val /= base;
    }

    for (int j = i - 1; j >= 0; j--)
        debug_putchar(buf[j]);
}

void debug_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            if (*fmt == '\n')
                debug_putchar('\r');
            debug_putchar(*fmt++);
            continue;
        }
        fmt++;

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            debug_puts(s ? s : "(null)");
            break;
        }
        case 'd': {
            int64_t val = va_arg(ap, int64_t);
            if (val < 0) {
                debug_putchar('-');
                val = -val;
            }
            debug_print_uint((uint64_t)val, 10);
            break;
        }
        case 'u': {
            uint64_t val = va_arg(ap, uint64_t);
            debug_print_uint(val, 10);
            break;
        }
        case 'x': {
            uint64_t val = va_arg(ap, uint64_t);
            debug_print_uint(val, 16);
            break;
        }
        case 'p': {
            uint64_t val = va_arg(ap, uint64_t);
            debug_puts("0x");
            debug_print_uint(val, 16);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            debug_putchar(c);
            break;
        }
        case '%':
            debug_putchar('%');
            break;
        default:
            debug_putchar('%');
            debug_putchar(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}
