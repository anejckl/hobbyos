#include "ramfs.h"
#include "vfs.h"
#include "../string.h"
#include "../debug/debug.h"

/* RAMFS read operation: copy file data to buffer */
static int ramfs_read(struct vfs_node *node, uint64_t offset, uint64_t size,
                      uint8_t *buffer) {
    if (!node || !node->data || !buffer)
        return -1;

    if (offset >= node->size)
        return 0;

    uint64_t remaining = node->size - offset;
    uint64_t to_read = size < remaining ? size : remaining;

    memcpy(buffer, node->data + offset, to_read);
    return (int)to_read;
}

/* RAMFS operations table */
static struct vfs_ops ramfs_ops = {
    .read = ramfs_read,
    .readdir = NULL,  /* Handled by VFS directly */
};

void ramfs_init(void) {
    debug_printf("RAMFS mounted at /\n");
}

int ramfs_add_file(const char *name, const uint8_t *data, uint64_t size) {
    struct vfs_node *node = vfs_register_node(name, VFS_FILE);
    if (!node) {
        debug_printf("ramfs: failed to register '%s'\n", name);
        return -1;
    }

    node->data = data;
    node->size = size;
    node->ops = &ramfs_ops;

    debug_printf("ramfs: added '%s' (%u bytes)\n", name, size);
    return 0;
}

const uint8_t *ramfs_get_file_data(const char *name, uint64_t *size_out) {
    struct vfs_node *node = vfs_lookup(name);
    if (!node && name[0] != '/') {
        /* Try with leading slash */
        char path[VFS_MAX_NAME + 1];
        path[0] = '/';
        strncpy(path + 1, name, VFS_MAX_NAME - 1);
        path[VFS_MAX_NAME] = '\0';
        node = vfs_lookup(path);
    }

    if (!node || node->type != VFS_FILE)
        return NULL;

    if (size_out)
        *size_out = node->size;
    return node->data;
}
