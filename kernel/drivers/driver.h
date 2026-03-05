#ifndef DRIVER_H
#define DRIVER_H

#include "../common.h"

/* Generic driver interface */
struct driver {
    const char *name;
    void (*init)(void);
    ssize_t (*read)(void *buf, size_t count);
    ssize_t (*write)(const void *buf, size_t count);
    void (*cleanup)(void);
};

#endif /* DRIVER_H */
