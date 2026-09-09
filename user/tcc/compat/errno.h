/* HobbyOS compat shim for TCC: errno.h.
 * No code in the vendored TCC source actually reads/writes errno anymore
 * (the one call site was inside the float-literal parser, removed) — this
 * exists only so the unconditional `#include <errno.h>` in tcc.h resolves. */
#ifndef HOBBYOS_TCC_ERRNO_H
#define HOBBYOS_TCC_ERRNO_H

extern int errno;

#define ERANGE 34
#define ENOENT 2
#define EEXIST 17

#endif
