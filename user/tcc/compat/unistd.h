/* HobbyOS compat shim for TCC: unistd.h.
 * POSIX-named wrappers around the existing sys_*_libc syscall wrappers,
 * implemented in user/tcc/hobbyos_compat.c. */
#ifndef HOBBYOS_TCC_UNISTD_H
#define HOBBYOS_TCC_UNISTD_H

#include "../../lib/libc.h"

int close(int fd);
int64_t read(int fd, void *buf, size_t count);
int64_t write(int fd, const void *buf, size_t count);
int64_t lseek(int fd, int64_t offset, int whence);
int unlink(const char *path);
char *getcwd(char *buf, size_t size);

#endif
