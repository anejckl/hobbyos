/* HobbyOS compat shim for TCC: fcntl.h.
 * O_RDONLY/O_WRONLY/O_RDWR/O_CREAT/O_TRUNC/O_APPEND already exist (matching
 * values) in the shared HobbyOS libc; O_BINARY is a Windows-only no-op. */
#ifndef HOBBYOS_TCC_FCNTL_H
#define HOBBYOS_TCC_FCNTL_H

#include "../../lib/libc.h"

#define O_BINARY 0

int open(const char *path, int flags, ...);

#endif
