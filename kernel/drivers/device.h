#ifndef DEVICE_H
#define DEVICE_H

#include "../common.h"

#define MAX_DEVICES 16

struct device {
    char name[16];
    int (*read)(struct device *dev, uint8_t *buf, uint32_t count);
    int (*write)(struct device *dev, const uint8_t *buf, uint32_t count);
    int (*ioctl)(struct device *dev, uint32_t cmd, uint64_t arg);
    void *private_data;
    bool in_use;
};

/* Initialize device registry */
void device_init(void);

/* Register a device. Returns pointer to device, or NULL on failure. */
struct device *device_register(const char *name,
    int (*read)(struct device *, uint8_t *, uint32_t),
    int (*write)(struct device *, const uint8_t *, uint32_t));

/* Find a device by name. Returns pointer, or NULL if not found. */
struct device *device_find(const char *name);

/* Get device count for enumeration */
int device_get_count(void);

/* Get device by index (for listing) */
struct device *device_get_by_index(int index);

#endif /* DEVICE_H */
