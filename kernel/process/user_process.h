#ifndef USER_PROCESS_H
#define USER_PROCESS_H

#include "../common.h"

/* Create a user-mode process from an embedded program binary.
 * name - process name
 * data - pointer to flat binary program data
 * size - size of program binary in bytes
 * ppid - parent process ID (0 = kernel/init)
 * Returns 0 on success, -1 on failure. */
int user_process_create(const char *name, const uint8_t *data, uint64_t size, uint32_t ppid);

#endif /* USER_PROCESS_H */
