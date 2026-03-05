#ifndef USER_PROGRAMS_H
#define USER_PROGRAMS_H

#include "common.h"

struct user_program {
    const char *name;
    const uint8_t *data;
    uint64_t size;
};

/* Find an embedded user program by name.
 * Returns pointer to program info, or NULL if not found. */
const struct user_program *user_programs_find(const char *name);

#endif /* USER_PROGRAMS_H */
