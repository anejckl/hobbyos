#ifndef VFS_H
#define VFS_H

#include "../common.h"

#define VFS_MAX_NAME    32
#define VFS_MAX_FDS     64
#define VFS_MAX_NODES   64

/* File types */
#define VFS_FILE        1
#define VFS_DIRECTORY   2

/* Forward declaration */
struct vfs_node;

/* Filesystem operations */
struct vfs_ops {
    int (*read)(struct vfs_node *node, uint64_t offset, uint64_t size,
                uint8_t *buffer);
    int (*write)(struct vfs_node *node, uint64_t offset, uint64_t size,
                 const uint8_t *buffer);
    int (*readdir)(struct vfs_node *node, uint32_t index, char *name_out,
                   uint32_t name_size);
};

/* VFS node (inode-like) */
struct vfs_node {
    char name[VFS_MAX_NAME];
    uint32_t type;          /* VFS_FILE or VFS_DIRECTORY */
    uint64_t size;
    const uint8_t *data;    /* For RAMFS: pointer to file data */
    struct vfs_ops *ops;
    uint32_t inode_num;     /* ext2 inode number (0 if not ext2) */
    bool in_use;
};

/* File descriptor */
struct vfs_fd {
    struct vfs_node *node;
    uint64_t offset;
    bool in_use;
};

/* Initialize the VFS subsystem */
void vfs_init(void);

/* Mount a filesystem at a prefix (e.g., "/proc").
 * Returns 0 on success, -1 on failure. */
int vfs_mount(const char *prefix, struct vfs_ops *ops);

/* Get mount ops for a path, or NULL if no mount matches. */
struct vfs_ops *vfs_get_mount_ops(const char *path);

/* Register a node in the root directory.
 * Returns pointer to the node, or NULL on failure. */
struct vfs_node *vfs_register_node(const char *name, uint32_t type);

/* Open a file by path. Returns fd index (>= 3), or -1 on failure. */
int vfs_open(const char *path);

/* Read from an open file descriptor.
 * Returns bytes read, or -1 on error. */
int vfs_read(int fd, uint8_t *buffer, uint64_t size);

/* Write to an open file descriptor.
 * Returns bytes written, or -1 on error. */
int vfs_write(int fd, const uint8_t *buffer, uint64_t size);

/* Close a file descriptor. Returns 0 on success, -1 on error. */
int vfs_close(int fd);

/* Read a directory entry by index.
 * Returns 0 on success, -1 if index is past end. */
int vfs_readdir(const char *path, uint32_t index, char *name_out,
                uint32_t name_size);

/* Look up a node by path. Returns node pointer, or NULL if not found. */
struct vfs_node *vfs_lookup(const char *path);

#endif /* VFS_H */
