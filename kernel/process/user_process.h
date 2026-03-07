#ifndef USER_PROCESS_H
#define USER_PROCESS_H

#include "../common.h"

/* Well-known address for passing arguments to user processes */
#define USER_ARGV_ADDR  0x600000ULL

/* Create a user-mode process from an embedded program binary.
 * name - process name
 * data - pointer to flat binary program data
 * size - size of program binary in bytes
 * ppid - parent process ID (0 = kernel/init)
 * Returns 0 on success, -1 on failure. */
int user_process_create(const char *name, const uint8_t *data, uint64_t size, uint32_t ppid);

/* Create a user-mode process with arguments.
 * args - argument string to pass to the process (copied to USER_ARGV_ADDR)
 * Returns 0 on success, -1 on failure. */
int user_process_create_args(const char *name, const uint8_t *data, uint64_t size,
                             uint32_t ppid, const char *args);

#endif /* USER_PROCESS_H */
