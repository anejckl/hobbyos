#ifndef DEVICE_H
#define DEVICE_H

#include "../common.h"

#define MAX_DEVICES    64
#define DEV_NAME_MAX   32

/* Device types */
#define DEV_CHAR    1
#define DEV_BLOCK   2
#define DEV_NET     3

/* Device states */
#define DEV_STATE_INACTIVE  0
#define DEV_STATE_ACTIVE    1

/* Forward declaration */
struct device;

/* Extended device operations (with offset for block devices) */
struct device_ops {
    int (*open)(struct device *dev);
    int (*close)(struct device *dev);
    int (*read)(struct device *dev, uint8_t *buf, uint32_t count, uint64_t offset);
    int (*write)(struct device *dev, const uint8_t *buf, uint32_t count, uint64_t offset);
    int (*ioctl)(struct device *dev, uint32_t cmd, uint64_t arg);
    int (*mmap)(struct device *dev, uint64_t offset, uint64_t size, uint64_t *phys_out);
};

struct device {
    char name[DEV_NAME_MAX];
    uint8_t type;       /* DEV_CHAR, DEV_BLOCK, DEV_NET */
    uint8_t state;      /* DEV_STATE_INACTIVE or DEV_STATE_ACTIVE */
    uint16_t major;
    uint16_t minor;
    /* Legacy read/write (without offset) — used by device_register() */
    int (*read)(struct device *dev, uint8_t *buf, uint32_t count);
    int (*write)(struct device *dev, const uint8_t *buf, uint32_t count);
    int (*ioctl)(struct device *dev, uint32_t cmd, uint64_t arg);
    /* Extended ops (with offset) — optional */
    struct device_ops *ops;
    void *private_data;
    bool in_use;
};

/* Initialize device registry */
void device_init(void);

/* Register a device (legacy API, backward compatible).
 * Returns pointer to device, or NULL on failure. */
struct device *device_register(const char *name,
    int (*read)(struct device *, uint8_t *, uint32_t),
    int (*write)(struct device *, const uint8_t *, uint32_t));

/* Register a device with full parameters.
 * Returns pointer to device, or NULL on failure. */
struct device *device_register_ex(const char *name, uint8_t type,
    uint16_t major, uint16_t minor, struct device_ops *ops);

/* Find a device by name. Returns pointer, or NULL if not found. */
struct device *device_find(const char *name);

/* Find a device by major/minor number. Returns pointer, or NULL. */
struct device *device_find_by_number(uint16_t major, uint16_t minor);

/* Get device count for enumeration */
int device_get_count(void);

/* Get device by index (for listing) */
struct device *device_get_by_index(int index);

#endif /* DEVICE_H */
