#include "vga.h"
#include "../string.h"

/* Use variadic arguments via GCC builtins */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

static uint16_t *vga_buffer;
static uint16_t vga_row;
static uint16_t vga_col;
static uint8_t  vga_color_attr;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static inline uint8_t vga_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static void vga_update_cursor(void) {
    uint16_t pos = vga_row * VGA_WIDTH + vga_col;
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

static void vga_scroll(void) {
    if (vga_row < VGA_HEIGHT)
        return;

    /* Move all lines up by one */
    memmove(vga_buffer, vga_buffer + VGA_WIDTH,
            (VGA_HEIGHT - 1) * VGA_WIDTH * 2);

    /* Clear the last line */
    for (int i = 0; i < VGA_WIDTH; i++)
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = vga_entry(' ', vga_color_attr);

    vga_row = VGA_HEIGHT - 1;
}

void vga_init(void) {
    vga_buffer = (uint16_t *)VGA_MEMORY;
    vga_row = 0;
    vga_col = 0;
    vga_color_attr = vga_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga_buffer[i] = vga_entry(' ', vga_color_attr);
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color_attr = vga_color(fg, bg);
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    } else if (c == '\b') {
        vga_backspace();
        return;
    } else {
        vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color_attr);
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    }
    vga_scroll();
    vga_update_cursor();
}

void vga_puts(const char *s) {
    while (*s)
        vga_putchar(*s++);
}

void vga_backspace(void) {
    if (vga_col > 0) {
        vga_col--;
    } else if (vga_row > 0) {
        vga_row--;
        vga_col = VGA_WIDTH - 1;
    }
    vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', vga_color_attr);
    vga_update_cursor();
}

uint16_t vga_get_row(void) { return vga_row; }
uint16_t vga_get_col(void) { return vga_col; }

/* Simple integer to string conversion */
static void print_uint(uint64_t val, int base, int pad_zero, int width) {
    char buf[24];
    int i = 0;
    const char *digits = "0123456789abcdef";

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = digits[val % base];
            val /= base;
        }
    }

    /* Pad if needed */
    while (i < width)
        buf[i++] = pad_zero ? '0' : ' ';

    /* Print in reverse */
    for (int j = i - 1; j >= 0; j--)
        vga_putchar(buf[j]);
}

static void print_int(int64_t val) {
    if (val < 0) {
        vga_putchar('-');
        val = -val;
    }
    print_uint((uint64_t)val, 10, 0, 0);
}

void vga_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            vga_putchar(*fmt++);
            continue;
        }
        fmt++;

        /* Check for zero-pad and width */
        int pad_zero = 0;
        int width = 0;
        if (*fmt == '0') {
            pad_zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            vga_puts(s ? s : "(null)");
            break;
        }
        case 'd': {
            int64_t val = va_arg(ap, int64_t);
            print_int(val);
            break;
        }
        case 'u': {
            uint64_t val = va_arg(ap, uint64_t);
            print_uint(val, 10, pad_zero, width);
            break;
        }
        case 'x': {
            uint64_t val = va_arg(ap, uint64_t);
            print_uint(val, 16, pad_zero, width);
            break;
        }
        case 'p': {
            uint64_t val = va_arg(ap, uint64_t);
            vga_puts("0x");
            print_uint(val, 16, 1, 16);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            vga_putchar(c);
            break;
        }
        case '%':
            vga_putchar('%');
            break;
        default:
            vga_putchar('%');
            vga_putchar(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}
