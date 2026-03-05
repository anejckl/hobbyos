#ifndef RAMFS_H
#define RAMFS_H

#include "../common.h"

/* Initialize the RAM filesystem and mount at root. */
void ramfs_init(void);

/* Add a file to the RAM filesystem.
 * name - filename (no path separator)
 * data - pointer to file data (must remain valid, e.g. .rodata)
 * size - size of file data in bytes
 * Returns 0 on success, -1 on failure. */
int ramfs_add_file(const char *name, const uint8_t *data, uint64_t size);

/* Get file data and size by name.
 * Returns pointer to data, or NULL if not found.
 * If size_out is non-NULL, stores file size there. */
const uint8_t *ramfs_get_file_data(const char *name, uint64_t *size_out);

#endif /* RAMFS_H */
