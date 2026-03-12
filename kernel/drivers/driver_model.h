#ifndef DRIVER_MODEL_H
#define DRIVER_MODEL_H

#include "../common.h"

#define MAX_DRIVERS 32

struct driver_info {
    char name[32];
    uint16_t major;
    int (*probe)(void);
    int (*init)(void);
    void (*remove)(void);
    bool registered;
};

/* Initialize the driver subsystem */
void driver_subsys_init(void);

/* Register a driver. Returns 0 on success, -1 on failure. */
int driver_register(struct driver_info *drv);

/* Probe and init all registered drivers */
void driver_probe_all(void);

/* Get driver count for enumeration */
int driver_get_count(void);

/* Get driver by index */
struct driver_info *driver_get_by_index(int index);

#endif /* DRIVER_MODEL_H */
