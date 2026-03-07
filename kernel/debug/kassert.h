#ifndef KASSERT_H
#define KASSERT_H

#include "../common.h"
#include "../debug/debug.h"

#define KASSERT(cond) do { \
    if (!(cond)) { \
        debug_printf("KASSERT FAILED: %s at %s:%d\n", \
                     #cond, __FILE__, __LINE__); \
        kpanic("assertion failed: " #cond); \
    } \
} while (0)

#endif /* KASSERT_H */
