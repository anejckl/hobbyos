/* HobbyOS compat shim for TCC: sys/time.h.
 * Only used by tcc.c's getclock_ms() for -bench compile-time reporting —
 * correctness doesn't matter, HobbyOS has no wall-clock time. */
#ifndef HOBBYOS_TCC_SYS_TIME_H
#define HOBBYOS_TCC_SYS_TIME_H

struct timeval {
    long tv_sec;
    long tv_usec;
};

int gettimeofday(struct timeval *tv, void *tz);

#endif
