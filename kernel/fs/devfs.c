#include "devfs.h"
#include "vfs.h"
#include "../drivers/device.h"
#include "../string.h"
#include "../debug/debug.h"

/* devfs read: delegates to device */
static int devfs_read(struct vfs_node *node, uint64_t offset, uint64_t size,
                      uint8_t *buffer) {
    /* Find device by node name (strip /dev/ prefix) */
    const char *devname = node->name;
    if (strncmp(devname, "/dev/", 5) == 0)
        devname += 5;

    struct device *dev = device_find(devname);
    if (!dev)
        return -1;

    /* Prefer device_ops->read (with offset) if available */
    if (dev->ops && dev->ops->read)
        return dev->ops->read(dev, buffer, (uint32_t)size, offset);
    /* Fall back to legacy read (no offset) */
    if (dev->read)
        return dev->read(dev, buffer, (uint32_t)size);
    return -1;
}

/* devfs write: delegates to device */
static int devfs_write(struct vfs_node *node, uint64_t offset, uint64_t size,
                       const uint8_t *buffer) {
    const char *devname = node->name;
    if (strncmp(devname, "/dev/", 5) == 0)
        devname += 5;

    struct device *dev = device_find(devname);
    if (!dev)
        return -1;

    /* Prefer device_ops->write (with offset) if available */
    if (dev->ops && dev->ops->write)
        return dev->ops->write(dev, buffer, (uint32_t)size, offset);
    /* Fall back to legacy write (no offset) */
    if (dev->write)
        return dev->write(dev, buffer, (uint32_t)size);
    return -1;
}

/* Check if a device name starts with a given prefix directory.
 * E.g., devname="input/mouse0", prefix="input" -> true */
static bool has_prefix_dir(const char *devname, const char *prefix, uint32_t prefix_len) {
    if (strncmp(devname, prefix, prefix_len) != 0)
        return false;
    return devname[prefix_len] == '/';
}

/* devfs readdir: list registered devices.
 * Supports hierarchical listing:
 *   - readdir of /dev lists top-level entries (devices without '/' and unique subdirs)
 *   - readdir of /dev/input lists devices under input/ prefix */
static int devfs_readdir(struct vfs_node *node, uint32_t index,
                         char *name_out, uint32_t name_size) {
    /* Determine the subdirectory prefix we are listing.
     * node->name is the path used to open, e.g. "/dev" or "/dev/input" */
    const char *subdir = NULL;
    uint32_t subdir_len = 0;
    const char *nodename = node->name;
    if (strncmp(nodename, "/dev/", 5) == 0 && nodename[5] != '\0') {
        subdir = nodename + 5;
        subdir_len = strlen(subdir);
    }

    /* We enumerate devices, tracking which entries we've output.
     * For top-level: output device names with no '/', and unique first-component
     * directory names for devices with '/'.
     * For subdir: output the part after "subdir/" for matching devices. */
    uint32_t out_idx = 0;

    /* Track already-emitted subdirectory prefixes (simple dedup) */
    char seen_dirs[16][DEV_NAME_MAX];
    int seen_count = 0;

    int total = device_get_count();
    for (int i = 0; i < total + MAX_DEVICES; i++) {
        /* device_get_by_index uses logical index among in_use devices */
        struct device *dev = device_get_by_index(i);
        if (!dev) break;

        const char *dname = dev->name;

        if (subdir) {
            /* Listing a subdirectory: only show devices under subdir/ */
            if (!has_prefix_dir(dname, subdir, subdir_len))
                continue;
            /* Extract the part after "subdir/" */
            const char *rest = dname + subdir_len + 1;
            /* Check if rest contains another '/' (nested subdir) */
            const char *slash = NULL;
            for (const char *p = rest; *p; p++) {
                if (*p == '/') { slash = p; break; }
            }
            if (slash) {
                /* Nested subdir — emit directory name */
                uint32_t dirlen = (uint32_t)(slash - rest);
                char dirname[DEV_NAME_MAX];
                if (dirlen >= DEV_NAME_MAX) dirlen = DEV_NAME_MAX - 1;
                memcpy(dirname, rest, dirlen);
                dirname[dirlen] = '\0';
                /* Dedup */
                bool dup = false;
                for (int s = 0; s < seen_count; s++) {
                    if (strcmp(seen_dirs[s], dirname) == 0) { dup = true; break; }
                }
                if (dup) continue;
                if (seen_count < 16) {
                    strncpy(seen_dirs[seen_count], dirname, DEV_NAME_MAX - 1);
                    seen_dirs[seen_count][DEV_NAME_MAX - 1] = '\0';
                    seen_count++;
                }
                if (out_idx == index) {
                    strncpy(name_out, dirname, name_size - 1);
                    name_out[name_size - 1] = '\0';
                    return 0;
                }
                out_idx++;
            } else {
                /* Leaf device */
                if (out_idx == index) {
                    strncpy(name_out, rest, name_size - 1);
                    name_out[name_size - 1] = '\0';
                    return 0;
                }
                out_idx++;
            }
        } else {
            /* Top-level /dev listing */
            const char *slash = NULL;
            for (const char *p = dname; *p; p++) {
                if (*p == '/') { slash = p; break; }
            }
            if (slash) {
                /* Device has subdirectory — emit directory component */
                uint32_t dirlen = (uint32_t)(slash - dname);
                char dirname[DEV_NAME_MAX];
                if (dirlen >= DEV_NAME_MAX) dirlen = DEV_NAME_MAX - 1;
                memcpy(dirname, dname, dirlen);
                dirname[dirlen] = '\0';
                /* Dedup */
                bool dup = false;
                for (int s = 0; s < seen_count; s++) {
                    if (strcmp(seen_dirs[s], dirname) == 0) { dup = true; break; }
                }
                if (dup) continue;
                if (seen_count < 16) {
                    strncpy(seen_dirs[seen_count], dirname, DEV_NAME_MAX - 1);
                    seen_dirs[seen_count][DEV_NAME_MAX - 1] = '\0';
                    seen_count++;
                }
                if (out_idx == index) {
                    strncpy(name_out, dirname, name_size - 1);
                    name_out[name_size - 1] = '\0';
                    return 0;
                }
                out_idx++;
            } else {
                /* Simple device name */
                if (out_idx == index) {
                    strncpy(name_out, dname, name_size - 1);
                    name_out[name_size - 1] = '\0';
                    return 0;
                }
                out_idx++;
            }
        }
    }

    return -1;
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
