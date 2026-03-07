#include "devfs.h"
#include "vfs.h"
#include "../drivers/device.h"
#include "../string.h"
#include "../debug/debug.h"

/* devfs read: delegates to device */
static int devfs_read(struct vfs_node *node, uint64_t offset, uint64_t size,
                      uint8_t *buffer) {
    (void)offset;
    /* Find device by node name (strip /dev/ prefix) */
    const char *devname = node->name;
    if (strncmp(devname, "/dev/", 5) == 0)
        devname += 5;

    struct device *dev = device_find(devname);
    if (!dev || !dev->read)
        return -1;
    return dev->read(dev, buffer, (uint32_t)size);
}

/* devfs write: delegates to device */
static int devfs_write(struct vfs_node *node, uint64_t offset, uint64_t size,
                       const uint8_t *buffer) {
    (void)offset;
    const char *devname = node->name;
    if (strncmp(devname, "/dev/", 5) == 0)
        devname += 5;

    struct device *dev = device_find(devname);
    if (!dev || !dev->write)
        return -1;
    return dev->write(dev, buffer, (uint32_t)size);
}

/* devfs readdir: list registered devices */
static int devfs_readdir(struct vfs_node *node, uint32_t index,
                         char *name_out, uint32_t name_size) {
    (void)node;
    struct device *dev = device_get_by_index((int)index);
    if (!dev)
        return -1;
    strncpy(name_out, dev->name, name_size - 1);
    name_out[name_size - 1] = '\0';
    return 0;
}

static struct vfs_ops devfs_ops = {
    .read = devfs_read,
    .write = devfs_write,
    .readdir = devfs_readdir,
};

void devfs_init(void) {
    vfs_mount("/dev", &devfs_ops);
    debug_printf("devfs: mounted at /dev\n");
}
