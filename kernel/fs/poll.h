#ifndef POLL_H
#define POLL_H

#include "../common.h"

struct process; /* forward decl */

/* Check if a file descriptor is readable for the given process. */
bool fd_is_readable(struct process *proc, int fd);

/* Check if a file descriptor is writable for the given process. */
bool fd_is_writable(struct process *proc, int fd);

#endif /* POLL_H */
