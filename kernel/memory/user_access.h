#ifndef USER_ACCESS_H
#define USER_ACCESS_H

#include "../common.h"

/* Copy data from user space to kernel space.
 * Validates that src range is below KERNEL_VMA.
 * Returns 0 on success, -1 if address invalid. */
int copy_from_user(void *dst, const void *src, size_t len);

/* Copy data from kernel space to user space.
 * Validates that dst range is below KERNEL_VMA.
 * Returns 0 on success, -1 if address invalid. */
int copy_to_user(void *dst, const void *src, size_t len);

/* Verify a user-space NUL-terminated string.
 * Returns string length if valid, -1 if invalid. */
int verify_user_string(const char *str, size_t max_len);

#endif /* USER_ACCESS_H */
