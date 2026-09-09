/* HobbyOS compat shim for TCC: stdlib.h
 * malloc/free/realloc/calloc/exit/abort/qsort/strtol/strtoul/atoi/getenv
 * all already exist with matching POSIX signatures in the shared HobbyOS
 * libc (linked in via the compiled user/lib sources) — just declare them. */
#ifndef HOBBYOS_TCC_STDLIB_H
#define HOBBYOS_TCC_STDLIB_H

#include "../../lib/libc.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/* long and long long are both 8 bytes on x86-64 — binary-identical width,
 * safe to alias rather than duplicate strtol/strtoul. */
#define strtoll  strtol
#define strtoull strtoul

#endif
