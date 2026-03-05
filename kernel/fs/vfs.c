#include "vfs.h"
#include "../string.h"
#include "../debug/debug.h"

static struct vfs_node nodes[VFS_MAX_NODES];
static struct vfs_fd fd_table[VFS_MAX_FDS];
static struct vfs_node root_node;

void vfs_init(void) {
    memset(nodes, 0, sizeof(nodes));
    memset(fd_table, 0, sizeof(fd_table));

    /* Reserve fd 0-2 for stdin/stdout/stderr */
    for (int i = 0; i < 3; i++)
        fd_table[i].in_use = true;

    /* Set up root directory node */
    strcpy(root_node.name, "/");
    root_node.type = VFS_DIRECTORY;
    root_node.in_use = true;

    debug_printf("VFS initialized\n");
}

struct vfs_node *vfs_register_node(const char *name, uint32_t type) {
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        if (!nodes[i].in_use) {
            memset(&nodes[i], 0, sizeof(struct vfs_node));
            strncpy(nodes[i].name, name, VFS_MAX_NAME - 1);
            nodes[i].name[VFS_MAX_NAME - 1] = '\0';
            nodes[i].type = type;
            nodes[i].in_use = true;
            return &nodes[i];
        }
    }
    return NULL;
}

struct vfs_node *vfs_lookup(const char *path) {
    if (!path)
        return NULL;

    /* Root directory */
    if (strcmp(path, "/") == 0)
        return &root_node;

    /* Skip leading slash */
    const char *name = path;
    if (name[0] == '/')
        name++;

    /* Empty name after slash */
    if (name[0] == '\0')
        return &root_node;

    /* Search nodes */
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        if (nodes[i].in_use && strcmp(nodes[i].name, name) == 0)
            return &nodes[i];
    }
    return NULL;
}

int vfs_open(const char *path) {
    struct vfs_node *node = vfs_lookup(path);
    if (!node)
        return -1;

    /* Find free fd (start at 3, skip stdin/stdout/stderr) */
    for (int i = 3; i < VFS_MAX_FDS; i++) {
        if (!fd_table[i].in_use) {
            fd_table[i].node = node;
            fd_table[i].offset = 0;
            fd_table[i].in_use = true;
            return i;
        }
    }
    return -1;  /* No free fds */
}

int vfs_read(int fd, uint8_t *buffer, uint64_t size) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].in_use)
        return -1;

    struct vfs_node *node = fd_table[fd].node;
    if (!node || !node->ops || !node->ops->read)
        return -1;

    int bytes = node->ops->read(node, fd_table[fd].offset, size, buffer);
    if (bytes > 0)
        fd_table[fd].offset += (uint64_t)bytes;
    return bytes;
}

int vfs_close(int fd) {
    if (fd < 3 || fd >= VFS_MAX_FDS || !fd_table[fd].in_use)
        return -1;

    fd_table[fd].node = NULL;
    fd_table[fd].offset = 0;
    fd_table[fd].in_use = false;
    return 0;
}

int vfs_readdir(const char *path, uint32_t index, char *name_out,
                uint32_t name_size) {
    /* Only support reading root directory */
    if (!path || (strcmp(path, "/") != 0 && strcmp(path, "") != 0))
        return -1;

    uint32_t count = 0;
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        if (nodes[i].in_use) {
            if (count == index) {
                strncpy(name_out, nodes[i].name, name_size - 1);
                name_out[name_size - 1] = '\0';
                return 0;
            }
            count++;
        }
    }
    return -1;  /* Past end */
}
