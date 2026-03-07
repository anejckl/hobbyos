#include "device.h"
#include "../string.h"
#include "../debug/debug.h"

static struct device devices[MAX_DEVICES];

void device_init(void) {
    memset(devices, 0, sizeof(devices));
    debug_printf("Device registry initialized\n");
}

struct device *device_register(const char *name,
    int (*read)(struct device *, uint8_t *, uint32_t),
    int (*write)(struct device *, const uint8_t *, uint32_t)) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i].in_use) {
            strncpy(devices[i].name, name, sizeof(devices[i].name) - 1);
            devices[i].name[sizeof(devices[i].name) - 1] = '\0';
            devices[i].read = read;
            devices[i].write = write;
            devices[i].ioctl = NULL;
            devices[i].private_data = NULL;
            devices[i].in_use = true;
            debug_printf("device: registered /dev/%s\n", name);
            return &devices[i];
        }
    }
    return NULL;
}

struct device *device_find(const char *name) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].in_use && strcmp(devices[i].name, name) == 0)
            return &devices[i];
    }
    return NULL;
}

int device_get_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].in_use)
            count++;
    }
    return count;
}

struct device *device_get_by_index(int index) {
    int count = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].in_use) {
            if (count == index)
                return &devices[i];
            count++;
        }
    }
    return NULL;
}
