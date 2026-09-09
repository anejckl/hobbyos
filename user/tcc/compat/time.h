/* HobbyOS compat shim for TCC: time.h.
 * Only reachable behind s1->do_debug (stab debug-info emission) — HobbyOS
 * has no calendar/wall-clock time (only a raw tick counter), so these are
 * harmless stubs; correctness of the returned value doesn't matter. */
#ifndef HOBBYOS_TCC_TIME_H
#define HOBBYOS_TCC_TIME_H

#include "../../lib/libc.h"

typedef int64_t time_t;

struct tm {
    int tm_sec, tm_min, tm_hour;
    int tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

time_t time(time_t *tloc);
struct tm *localtime(const time_t *timep);

#endif
