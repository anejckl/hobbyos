#include "libc.h"

/* vsnprintf: supports %d %u %x %s %c %p %% %ld %lu %lx */
int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap) {
    size_t pos = 0;

#define PUTC(c) do { if (pos + 1 < size) buf[pos++] = (c); } while (0)

    for (const char *f = fmt; *f; f++) {
        if (*f != '%') {
            PUTC(*f);
            continue;
        }
        f++;

        /* Flags */
        int zero_pad = 0, left_align = 0;
        while (*f == '0' || *f == '-') {
            if (*f == '0') zero_pad = 1;
            if (*f == '-') left_align = 1;
            f++;
        }

        /* Width */
        int width = 0;
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f++;
        }

        /* Long prefix */
        int is_long = 0;
        if (*f == 'l') { is_long = 1; f++; }

        char tmp[64];
        int tlen = 0;
        const char *sval = NULL;

        switch (*f) {
        case 'd': {
            int64_t v = is_long ? (int64_t)__builtin_va_arg(ap, long long)
                                : (int64_t)__builtin_va_arg(ap, int);
            int neg = (v < 0);
            uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
            if (u == 0) { tmp[tlen++] = '0'; }
            while (u > 0) { tmp[tlen++] = '0' + (int)(u % 10); u /= 10; }
            if (neg) tmp[tlen++] = '-';
            /* reverse */
            for (int i = 0, j = tlen-1; i < j; i++, j--) {
                char t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
            }
            break;
        }
        case 'u': {
            uint64_t v = is_long ? __builtin_va_arg(ap, unsigned long long)
                                 : (uint64_t)__builtin_va_arg(ap, unsigned int);
            if (v == 0) { tmp[tlen++] = '0'; }
            while (v > 0) { tmp[tlen++] = '0' + (int)(v % 10); v /= 10; }
            for (int i = 0, j = tlen-1; i < j; i++, j--) {
                char t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
            }
            break;
        }
        case 'x':
        case 'p': {
            uint64_t v = (*f == 'p' || is_long)
                ? __builtin_va_arg(ap, unsigned long long)
                : (uint64_t)__builtin_va_arg(ap, unsigned int);
            if (v == 0) { tmp[tlen++] = '0'; }
            while (v > 0) {
                int d = (int)(v & 0xf);
                tmp[tlen++] = d < 10 ? '0' + d : 'a' + d - 10;
                v >>= 4;
            }
            for (int i = 0, j = tlen-1; i < j; i++, j--) {
                char t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
            }
            if (*f == 'p') { /* prepend 0x */ }
            break;
        }
        case 's': {
            sval = __builtin_va_arg(ap, const char *);
            if (!sval) sval = "(null)";
            break;
        }
        case 'c': {
            tmp[0] = (char)__builtin_va_arg(ap, int);
            tlen = 1;
            break;
        }
        case '%':
            PUTC('%');
            continue;
        default:
            PUTC('%');
            PUTC(*f);
            continue;
        }

        if (sval) {
            size_t slen = strlen(sval);
            if (!left_align) {
                for (size_t k = slen; (int)k < width; k++) PUTC(' ');
            }
            for (size_t k = 0; k < slen; k++) PUTC(sval[k]);
            if (left_align) {
                for (size_t k = slen; (int)k < width; k++) PUTC(' ');
            }
        } else {
            char pad_c = (zero_pad && !left_align) ? '0' : ' ';
            if (!left_align) {
                for (int k = tlen; k < width; k++) PUTC(pad_c);
            }
            for (int k = 0; k < tlen; k++) PUTC(tmp[k]);
            if (left_align) {
                for (int k = tlen; k < width; k++) PUTC(' ');
            }
        }
    }

    if (size > 0) buf[pos] = '\0';
    return (int)pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

int printf(const char *fmt, ...) {
    char buf[1024];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    sys_write_libc(1, buf, (size_t)n);
    return n;
}

int puts(const char *s) {
    size_t len = strlen(s);
    sys_write_libc(1, s, len);
    sys_write_libc(1, "\n", 1);
    return (int)len + 1;
}

int fputs(const char *s, int fd) {
    size_t len = strlen(s);
    return (int)sys_write_libc(fd, s, len);
}

int fgets(char *buf, int size, int fd) {
    if (size <= 0) return 0;
    int n = 0;
    while (n < size - 1) {
        char c;
        int64_t r = sys_read_libc(fd, &c, 1);
        if (r <= 0) break;
        buf[n++] = c;
        if (c == '\n') break;
    }
    buf[n] = '\0';
    return n;
}

int fprintf(int fd, const char *fmt, ...) {
    char buf[1024];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    sys_write_libc(fd, buf, (size_t)n);
    return n;
}

int putchar(int c) {
    char ch = (char)c;
    sys_write_libc(1, &ch, 1);
    return c;
}

int fputc(int c, int fd) {
    char ch = (char)c;
    sys_write_libc(fd, &ch, 1);
    return c;
}

int getchar(void) {
    char c;
    int64_t r = sys_read_libc(0, &c, 1);
    return r <= 0 ? -1 : (int)(unsigned char)c;
}
