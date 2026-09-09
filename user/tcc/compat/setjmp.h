/* HobbyOS compat shim for TCC: setjmp.h via GCC builtins.
 * __builtin_setjmp/__builtin_longjmp are freestanding-safe (no libc, no
 * hand-written asm) and only touch integer/pointer registers — no SSE/FPU
 * state involved, so this is safe under this kernel's -mno-sse. */
#ifndef HOBBYOS_TCC_SETJMP_H
#define HOBBYOS_TCC_SETJMP_H

/* GCC requires at least 5 pointer-sized words. jmp_buf is itself an array
 * type (not a struct) so it decays to a pointer at call sites, matching
 * the real POSIX jmp_buf convention TCC's source already assumes. */
typedef void *jmp_buf[5];

#define setjmp(buf)        __builtin_setjmp(buf)
#define longjmp(buf, val)  __builtin_longjmp(buf, val)

#endif
