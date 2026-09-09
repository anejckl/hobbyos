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

static int digit_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

long strtol(const char *s, char **endptr, int base) {
    int neg = 0;
    long n = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (base == 0) {
        base = (s[0] == '0') ? 8 : 10;
    }

    const char *start = s;
    while (1) {
        int d = digit_value((unsigned char)*s);
        if (d < 0 || d >= base) break;
        n = n * base + d;
        s++;
    }
    if (endptr) *endptr = (char *)(s == start ? start : s);
    return neg ? -n : n;
}

unsigned long strtoul(const char *s, char **endptr, int base) {
    unsigned long n = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else if (base == 0) {
        base = (s[0] == '0') ? 8 : 10;
    }

    const char *start = s;
    while (1) {
        int d = digit_value((unsigned char)*s);
        if (d < 0 || d >= base) break;
        n = n * (unsigned long)base + (unsigned long)d;
        s++;
    }
    if (endptr) *endptr = (char *)(s == start ? start : s);
    return n;
}

static void qsort_swap(char *a, char *b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

/* Recurses only into the smaller partition (bounds stack depth to O(log n)
 * even on adversarial input); insertion-sort cutoff for small partitions. */
static void qsort_rec(char *arr, size_t lo, size_t hi, size_t size,
                       int (*compar)(const void *, const void *)) {
    while (lo + 1 < hi) {
        if (hi - lo <= 12) {
            for (size_t i = lo + 1; i < hi; i++) {
                size_t j = i;
                while (j > lo && compar(arr + (j - 1) * size, arr + j * size) > 0) {
                    qsort_swap(arr + j * size, arr + (j - 1) * size, size);
                    j--;
                }
            }
            return;
        }

        size_t mid = lo + (hi - lo) / 2;
        qsort_swap(arr + mid * size, arr + lo * size, size);
        char *pivot = arr + lo * size;
        size_t i = lo + 1, j = hi;
        while (1) {
            while (i < j && compar(arr + i * size, pivot) <= 0) i++;
            while (j > i && compar(arr + (j - 1) * size, pivot) > 0) j--;
            if (i >= j) break;
            qsort_swap(arr + i * size, arr + (j - 1) * size, size);
            i++;
        }
        qsort_swap(pivot, arr + (i - 1) * size, size);
        size_t split = i - 1;

        if (split - lo < hi - split) {
            qsort_rec(arr, lo, split, size, compar);
            lo = split + 1;
        } else {
            qsort_rec(arr, split + 1, hi, size, compar);
            hi = split;
        }
    }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    if (nmemb < 2 || size == 0) return;
    qsort_rec((char *)base, 0, nmemb, size, compar);
}

char *getenv(const char *name) {
    (void)name;
    /* No process ever receives envp (kernel/elf/elf_loader.c hardcodes
     * envp[0] = NULL) — always report "not set" rather than pretending
     * to support an environment that doesn't exist. */
    return NULL;
}
