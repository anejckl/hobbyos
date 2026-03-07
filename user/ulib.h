#ifndef ULIB_H
#define ULIB_H

#include "syscall.h"

static inline size_t ulib_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline int ulib_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(*(unsigned char *)a) - (int)(*(unsigned char *)b);
}

static inline void print(const char *s) {
    sys_write(1, s, ulib_strlen(s));
}

static inline void print_num(uint64_t n) {
    char buf[20];
    int i = 0;
    if (n == 0) {
        sys_write(1, "0", 1);
        return;
    }
    while (n > 0) {
        buf[i++] = '0' + (char)(n % 10);
        n /= 10;
    }
    /* Reverse */
    char out[20];
    for (int j = 0; j < i; j++)
        out[j] = buf[i - 1 - j];
    sys_write(1, out, (uint64_t)i);
}

static inline void print_char(char c) {
    sys_write(1, &c, 1);
}

/* Get argv string from well-known address */
static inline const char *get_argv(void) {
    const char *p = (const char *)USER_ARGV_ADDR;
    /* Check if the page was mapped (first byte not zero means args present) */
    return p[0] ? p : NULL;
}

#endif /* ULIB_H */
