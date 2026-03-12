#include "driver_model.h"
#include "../string.h"
#include "../debug/debug.h"

static struct driver_info *drivers[MAX_DRIVERS];
static int driver_count = 0;

void driver_subsys_init(void) {
    driver_count = 0;
    memset(drivers, 0, sizeof(drivers));
    debug_printf("driver subsystem initialized (%d slots)\n", (int64_t)MAX_DRIVERS);
}

int driver_register(struct driver_info *drv) {
    if (!drv || driver_count >= MAX_DRIVERS)
        return -1;
    drv->registered = true;
    drivers[driver_count++] = drv;
    debug_printf("driver: registered '%s' (major=%u)\n",
                 drv->name, (uint64_t)drv->major);
    return 0;
}

void driver_probe_all(void) {
    for (int i = 0; i < driver_count; i++) {
        struct driver_info *drv = drivers[i];
        if (!drv || !drv->registered)
            continue;

        /* Probe: check if hardware is present */
        if (drv->probe) {
            int ret = drv->probe();
            if (ret != 0) {
                debug_printf("driver: '%s' probe failed\n", drv->name);
                continue;
            }
        }

        /* Init: initialize the driver */
        if (drv->init) {
            int ret = drv->init();
            if (ret != 0) {
                debug_printf("driver: '%s' init failed\n", drv->name);
                continue;
            }
        }

        debug_printf("driver: '%s' probed and initialized\n", drv->name);
    }
}

int driver_get_count(void) {
    return driver_count;
}

struct driver_info *driver_get_by_index(int index) {
    if (index < 0 || index >= driver_count)
        return NULL;
    return drivers[index];
}
