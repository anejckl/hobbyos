#include "user_access.h"
#include "../string.h"

int copy_from_user(void *dst, const void *src, size_t len) {
    uint64_t start = (uint64_t)src;
    uint64_t end = start + len;

    /* Validate entire range is in user space */
    if (start >= KERNEL_VMA || end >= KERNEL_VMA || end < start)
        return -1;

    memcpy(dst, src, len);
    return 0;
}

int copy_to_user(void *dst, const void *src, size_t len) {
    uint64_t start = (uint64_t)dst;
    uint64_t end = start + len;

    if (start >= KERNEL_VMA || end >= KERNEL_VMA || end < start)
        return -1;

    memcpy(dst, src, len);
    return 0;
}

int verify_user_string(const char *str, size_t max_len) {
    uint64_t addr = (uint64_t)str;
    if (addr >= KERNEL_VMA)
        return -1;

    for (size_t i = 0; i < max_len; i++) {
        if (addr + i >= KERNEL_VMA)
            return -1;
        if (str[i] == '\0')
            return (int)i;
    }

    return -1;  /* No NUL found within max_len */
}
