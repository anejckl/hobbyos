/* HobbyOS compat shim for TCC: inttypes.h.
 * elf.h only needs the fixed-width integer typedefs, already provided by
 * the shared HobbyOS libc (uint8_t/uint16_t/uint32_t/uint64_t/int32_t/
 * int64_t) — no PRId64-style format macros are used anywhere in the
 * vendored TCC source. */
#ifndef HOBBYOS_TCC_INTTYPES_H
#define HOBBYOS_TCC_INTTYPES_H

#include "../../lib/libc.h"

/* Missing from the shared libc's type set — TCC-private additions. */
typedef signed char  int8_t;
typedef short         int16_t;
typedef uint64_t      uintptr_t;
typedef int64_t        intptr_t;

#endif
