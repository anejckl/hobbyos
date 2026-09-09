/* Milestone 4 fixture: tcc compiles a program against the real extended
 * libc (printf, malloc/free, fork/wait) instead of raw syscalls only.
 * Linked against a precompiled (host-toolchain-built) libc object
 * (hobbyc.o), not compiled by tcc itself: real upstream TCC has no
 * target-side __builtin_va_list/va_arg support on x86-64 outside its own
 * runtime library (which this port doesn't ship, see the tccrun.c
 * exclusion note in the Makefile) — calling a variadic function like
 * printf needs no such support on the caller side, only inside its own
 * implementation, so this sidesteps the gap while still exercising tcc's
 * real static linker against externally-supplied object code. */
#include "libc.h"

void _start(void) {
    int a = 6, b = 7;
    printf("tcc libc demo: %d * %d = %d\n", a, b, a * b);

    char *buf = (char *)malloc(64);
    if (!buf) {
        printf("tcc libc demo: malloc failed\n");
        sys_exit(1);
    }
    strcpy(buf, "malloc works");
    printf("tcc libc demo: %s\n", buf);
    free(buf);

    int64_t pid = sys_fork_libc();
    if (pid == 0) {
        printf("tcc libc demo: child running\n");
        sys_exit(77);
    } else {
        int32_t status = 0;
        sys_wait_libc(&status);
        printf("tcc libc demo: child exited with status %d\n", status);
    }

    printf("tcc libc demo: all done\n");
    sys_exit(0);
}
