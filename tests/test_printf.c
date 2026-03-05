/*
 * test_printf.c — Unit tests for vga_printf integer formatting.
 *
 * We replicate the kernel's print_uint / print_int / vga_printf logic
 * and redirect output to a test buffer via a mock vga_putchar.
 */

#include "test_main.h"
#include <string.h>

/* ---- Mock output buffer ---- */
static char out_buf[1024];
static int out_pos;

static void out_reset(void) {
    out_pos = 0;
    memset(out_buf, 0, sizeof(out_buf));
}

/* Mock vga_putchar — appends to out_buf */
static void mock_putchar(char c) {
    if (out_pos < (int)sizeof(out_buf) - 1)
        out_buf[out_pos++] = c;
}

/* Mock vga_puts */
static void mock_puts(const char *s) {
    while (*s)
        mock_putchar(*s++);
}

/* ---- Replicate kernel formatting logic ---- */

static void test_print_uint(uint64_t val, int base, int pad_zero, int width) {
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

    while (i < width)
        buf[i++] = pad_zero ? '0' : ' ';

    for (int j = i - 1; j >= 0; j--)
        mock_putchar(buf[j]);
}

static void test_print_int(int64_t val) {
    if (val < 0) {
        mock_putchar('-');
        val = -val;
    }
    test_print_uint((uint64_t)val, 10, 0, 0);
}

/* Simplified vga_printf clone using mock output */
static void mock_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            mock_putchar(*fmt++);
            continue;
        }
        fmt++;

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
            mock_puts(s ? s : "(null)");
            break;
        }
        case 'd': {
            int64_t val = va_arg(ap, int64_t);
            test_print_int(val);
            break;
        }
        case 'u': {
            uint64_t val = va_arg(ap, uint64_t);
            test_print_uint(val, 10, pad_zero, width);
            break;
        }
        case 'x': {
            uint64_t val = va_arg(ap, uint64_t);
            test_print_uint(val, 16, pad_zero, width);
            break;
        }
        case 'p': {
            uint64_t val = va_arg(ap, uint64_t);
            mock_puts("0x");
            test_print_uint(val, 16, 1, 16);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            mock_putchar(c);
            break;
        }
        case '%':
            mock_putchar('%');
            break;
        default:
            mock_putchar('%');
            mock_putchar(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}

/* ---- Tests ---- */

void test_printf_string(void) {
    out_reset();
    mock_printf("hello %s!", "world");
    TEST("printf %%s", strcmp(out_buf, "hello world!") == 0);

    out_reset();
    mock_printf("%s", (const char *)NULL);
    TEST("printf %%s NULL", strcmp(out_buf, "(null)") == 0);
}

void test_printf_decimal(void) {
    out_reset();
    mock_printf("%d", (int64_t)42);
    TEST("printf %%d positive", strcmp(out_buf, "42") == 0);

    out_reset();
    mock_printf("%d", (int64_t)-7);
    TEST("printf %%d negative", strcmp(out_buf, "-7") == 0);

    out_reset();
    mock_printf("%d", (int64_t)0);
    TEST("printf %%d zero", strcmp(out_buf, "0") == 0);
}

void test_printf_unsigned(void) {
    out_reset();
    mock_printf("%u", (uint64_t)100);
    TEST("printf %%u basic", strcmp(out_buf, "100") == 0);

    out_reset();
    mock_printf("%u", (uint64_t)0);
    TEST("printf %%u zero", strcmp(out_buf, "0") == 0);
}

void test_printf_hex(void) {
    out_reset();
    mock_printf("%x", (uint64_t)0xFF);
    TEST("printf %%x", strcmp(out_buf, "ff") == 0);

    out_reset();
    mock_printf("%x", (uint64_t)0);
    TEST("printf %%x zero", strcmp(out_buf, "0") == 0);

    out_reset();
    mock_printf("%08x", (uint64_t)0x1A);
    TEST("printf %%08x pad", strcmp(out_buf, "0000001a") == 0);
}

void test_printf_pointer(void) {
    out_reset();
    mock_printf("%p", (uint64_t)0xDEADBEEF);
    TEST("printf %%p", strcmp(out_buf, "0x00000000deadbeef") == 0);

    out_reset();
    mock_printf("%p", (uint64_t)0);
    TEST("printf %%p zero", strcmp(out_buf, "0x0000000000000000") == 0);
}

void test_printf_char(void) {
    out_reset();
    mock_printf("%c", 'A');
    TEST("printf %%c", strcmp(out_buf, "A") == 0);
}

void test_printf_percent(void) {
    out_reset();
    mock_printf("100%%");
    TEST("printf %%%%", strcmp(out_buf, "100%") == 0);
}

void test_printf_mixed(void) {
    out_reset();
    mock_printf("[%s] %d 0x%x", "test", (int64_t)10, (uint64_t)255);
    TEST("printf mixed", strcmp(out_buf, "[test] 10 0xff") == 0);
}

/* ---- Suite entry point ---- */

void test_printf_suite(void) {
    printf("=== Printf formatting tests ===\n");
    test_printf_string();
    test_printf_decimal();
    test_printf_unsigned();
    test_printf_hex();
    test_printf_pointer();
    test_printf_char();
    test_printf_percent();
    test_printf_mixed();
}
