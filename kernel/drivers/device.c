#include "device.h"
#include "../string.h"
#include "../debug/debug.h"

static struct device devices[MAX_DEVICES];

void device_init(void) {
    memset(devices, 0, sizeof(devices));
    debug_printf("Device registry initialized (%d slots)\n", (int64_t)MAX_DEVICES);
}

struct device *device_register(const char *name,
    int (*read)(struct device *, uint8_t *, uint32_t),
    int (*write)(struct device *, const uint8_t *, uint32_t)) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i].in_use) {
            memset(&devices[i], 0, sizeof(struct device));
            strncpy(devices[i].name, name, DEV_NAME_MAX - 1);
            devices[i].name[DEV_NAME_MAX - 1] = '\0';
            devices[i].type = DEV_CHAR;
            devices[i].state = DEV_STATE_ACTIVE;
            devices[i].read = read;
            devices[i].write = write;
            devices[i].ioctl = NULL;
            devices[i].ops = NULL;
            devices[i].private_data = NULL;
            devices[i].in_use = true;
            debug_printf("device: registered /dev/%s\n", name);
            return &devices[i];
        }
    }
    return NULL;
}

struct device *device_register_ex(const char *name, uint8_t type,
    uint16_t major, uint16_t minor, struct device_ops *ops) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i].in_use) {
            memset(&devices[i], 0, sizeof(struct device));
            strncpy(devices[i].name, name, DEV_NAME_MAX - 1);
            devices[i].name[DEV_NAME_MAX - 1] = '\0';
            devices[i].type = type;
            devices[i].state = DEV_STATE_ACTIVE;
            devices[i].major = major;
            devices[i].minor = minor;
            devices[i].ops = ops;
            devices[i].in_use = true;
            debug_printf("device: registered /dev/%s (major=%u minor=%u)\n",
                         name, (uint64_t)major, (uint64_t)minor);
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

struct device *device_find_by_number(uint16_t major, uint16_t minor) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].in_use &&
            devices[i].major == major && devices[i].minor == minor)
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
