#ifndef NETBUF_H
#define NETBUF_H

#include "../common.h"

/* Two pool sizes for network buffers */
#define NETBUF_SMALL_SIZE    256
#define NETBUF_LARGE_SIZE    2048
#define NETBUF_SMALL_COUNT   128
#define NETBUF_LARGE_COUNT   128

/* Legacy compatibility */
#define NETBUF_SIZE          NETBUF_LARGE_SIZE
#define NETBUF_POOL_SIZE     NETBUF_LARGE_COUNT

struct netbuf {
    uint8_t data[NETBUF_LARGE_SIZE]; /* max size buffer */
    uint32_t len;       /* actual data length */
    uint32_t offset;    /* start of payload within data[] */
    uint32_t capacity;  /* NETBUF_SMALL_SIZE or NETBUF_LARGE_SIZE */
    uint8_t refcount;   /* reference count */
    bool in_use;
    bool is_small;      /* true if from small pool */
};

void netbuf_init(void);

/* Allocate a netbuf of at least min_size bytes.
 * If min_size <= NETBUF_SMALL_SIZE, tries small pool first. */
struct netbuf *netbuf_alloc_size(uint32_t min_size);

/* Legacy: allocate large buffer */
struct netbuf *netbuf_alloc(void);

/* Increment reference count */
void netbuf_ref(struct netbuf *buf);

/* Decrement reference count; free if zero */
void netbuf_free(struct netbuf *buf);

#endif /* NETBUF_H */
