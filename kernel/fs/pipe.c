#include "pipe.h"
#include "../memory/kheap.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../string.h"
#include "../debug/debug.h"

int pipe_create(struct pipe **out) {
    struct pipe *p = (struct pipe *)kmalloc(sizeof(struct pipe));
    if (!p)
        return -1;

    memset(p, 0, sizeof(struct pipe));
    p->readers = 1;
    p->writers = 1;

    *out = p;
    debug_printf("pipe: created pipe at 0x%x\n", (uint64_t)p);
    return 0;
}

int pipe_read(struct pipe *p, uint8_t *buf, uint32_t count) {
    if (!p)
        return -1;

    /* If buffer is empty, check if there are writers */
    while (p->count == 0) {
        if (p->writers == 0)
            return 0;  /* EOF: no writers left */

        /* Block until data is available */
        struct process *cur = scheduler_get_current();
        cur->state = PROCESS_BLOCKED;
        p->blocked_reader = cur;
        schedule();
        sti();
        /* Woken up — retry */
    }

    /* Read from ring buffer */
    uint32_t to_read = count < p->count ? count : p->count;
    for (uint32_t i = 0; i < to_read; i++) {
        buf[i] = p->buffer[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count -= to_read;

    /* Wake blocked writer if any */
    if (p->blocked_writer) {
        p->blocked_writer->state = PROCESS_READY;
        scheduler_add(p->blocked_writer);
        p->blocked_writer = NULL;
    }

    return (int)to_read;
}

int pipe_write(struct pipe *p, const uint8_t *buf, uint32_t count) {
    if (!p)
        return -1;

    /* No readers — broken pipe */
    if (p->readers == 0)
        return -1;

    /* If buffer is full, block */
    while (p->count == PIPE_BUF_SIZE) {
        if (p->readers == 0)
            return -1;  /* Broken pipe */

        struct process *cur = scheduler_get_current();
        cur->state = PROCESS_BLOCKED;
        p->blocked_writer = cur;
        schedule();
        sti();
        /* Woken up — retry */
    }

    /* Write to ring buffer */
    uint32_t space = PIPE_BUF_SIZE - p->count;
    uint32_t to_write = count < space ? count : space;
    for (uint32_t i = 0; i < to_write; i++) {
        p->buffer[p->write_pos] = buf[i];
        p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count += to_write;

    /* Wake blocked reader if any */
    if (p->blocked_reader) {
        p->blocked_reader->state = PROCESS_READY;
        scheduler_add(p->blocked_reader);
        p->blocked_reader = NULL;
    }

    return (int)to_write;
}

void pipe_close_read(struct pipe *p) {
    if (!p)
        return;
    if (p->readers > 0)
        p->readers--;

    /* Wake blocked writer — they'll get broken pipe */
    if (p->readers == 0 && p->blocked_writer) {
        p->blocked_writer->state = PROCESS_READY;
        scheduler_add(p->blocked_writer);
        p->blocked_writer = NULL;
    }
}

void pipe_close_write(struct pipe *p) {
    if (!p)
        return;
    if (p->writers > 0)
        p->writers--;

    /* Wake blocked reader — they'll get EOF */
    if (p->writers == 0 && p->blocked_reader) {
        p->blocked_reader->state = PROCESS_READY;
        scheduler_add(p->blocked_reader);
        p->blocked_reader = NULL;
    }
}

void pipe_inc_ref(struct pipe *p, int type) {
    if (!p)
        return;
    if (type == FD_PIPE_READ)
        p->readers++;
    else if (type == FD_PIPE_WRITE)
        p->writers++;
}
