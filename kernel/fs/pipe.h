#ifndef PIPE_H
#define PIPE_H

#include "../common.h"

#define PIPE_BUF_SIZE 4096

struct process; /* forward declaration */

struct pipe {
    uint8_t buffer[PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;          /* bytes in buffer */
    uint32_t readers;        /* number of open read ends */
    uint32_t writers;        /* number of open write ends */
    struct process *blocked_reader;  /* process blocked on read */
    struct process *blocked_writer;  /* process blocked on write */
};

/* Create a new pipe. Returns 0 on success, -1 on failure. */
int pipe_create(struct pipe **out);

/* Read from pipe. Blocks if empty and writers > 0.
 * Returns bytes read, or 0 on EOF (no writers). */
int pipe_read(struct pipe *p, uint8_t *buf, uint32_t count);

/* Write to pipe. Blocks if full and readers > 0.
 * Returns bytes written, or -1 on broken pipe (no readers). */
int pipe_write(struct pipe *p, const uint8_t *buf, uint32_t count);

/* Close read end. Decrements readers count. */
void pipe_close_read(struct pipe *p);

/* Close write end. Decrements writers count, wakes blocked reader. */
void pipe_close_write(struct pipe *p);

/* Increment reference count for a pipe end (used by fork/dup2).
 * type should be FD_PIPE_READ or FD_PIPE_WRITE. */
void pipe_inc_ref(struct pipe *p, int type);

#endif /* PIPE_H */
