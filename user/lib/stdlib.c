#include "libc.h"

int atoi(const char *s) {
    int neg = 0, n = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

long long atol(const char *s) {
    int neg = 0;
    long long n = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

void exit(int status) {
    sys_exit(status);
}

void abort(void) {
    sys_exit(-1);
}

char *itoa(int value, char *buf, int base) {
    char tmp[64];
    int neg = 0, pos = 0;
    if (value < 0 && base == 10) { neg = 1; value = -value; }
    unsigned int uval = (unsigned int)value;
    if (uval == 0) { tmp[pos++] = '0'; }
    while (uval > 0) {
        int d = uval % base;
        tmp[pos++] = (d < 10) ? '0' + d : 'a' + d - 10;
        uval /= base;
    }
    if (neg) tmp[pos++] = '-';
    int i = 0;
    while (pos > 0) buf[i++] = tmp[--pos];
    buf[i] = '\0';
    return buf;
}

char *utoa(uint64_t value, char *buf, int base) {
    char tmp[64];
    int pos = 0;
    if (value == 0) { tmp[pos++] = '0'; }
    while (value > 0) {
        int d = (int)(value % (uint64_t)base);
        tmp[pos++] = (d < 10) ? '0' + d : 'a' + d - 10;
        value /= (uint64_t)base;
    }
    int i = 0;
    while (pos > 0) buf[i++] = tmp[--pos];
    buf[i] = '\0';
    return buf;
}
