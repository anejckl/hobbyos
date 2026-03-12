#include "netbuf.h"
#include "../string.h"
#include "../debug/debug.h"

/* Small buffer pool (for ACKs, small packets) */
static struct netbuf small_pool[NETBUF_SMALL_COUNT];

/* Large buffer pool (for data packets) */
static struct netbuf large_pool[NETBUF_LARGE_COUNT];

void netbuf_init(void) {
    memset(small_pool, 0, sizeof(small_pool));
    memset(large_pool, 0, sizeof(large_pool));

    for (int i = 0; i < NETBUF_SMALL_COUNT; i++) {
        small_pool[i].capacity = NETBUF_SMALL_SIZE;
        small_pool[i].is_small = true;
    }
    for (int i = 0; i < NETBUF_LARGE_COUNT; i++) {
        large_pool[i].capacity = NETBUF_LARGE_SIZE;
        large_pool[i].is_small = false;
    }

    debug_printf("netbuf: pool initialized (%u small + %u large buffers)\n",
                 (uint64_t)NETBUF_SMALL_COUNT, (uint64_t)NETBUF_LARGE_COUNT);
}

struct netbuf *netbuf_alloc_size(uint32_t min_size) {
    /* Try small pool first if size fits */
    if (min_size <= NETBUF_SMALL_SIZE) {
        for (int i = 0; i < NETBUF_SMALL_COUNT; i++) {
            if (!small_pool[i].in_use) {
                small_pool[i].in_use = true;
                small_pool[i].len = 0;
                small_pool[i].offset = 0;
                small_pool[i].refcount = 1;
                return &small_pool[i];
            }
        }
    }

    /* Large pool */
    for (int i = 0; i < NETBUF_LARGE_COUNT; i++) {
        if (!large_pool[i].in_use) {
            large_pool[i].in_use = true;
            large_pool[i].len = 0;
            large_pool[i].offset = 0;
            large_pool[i].refcount = 1;
            return &large_pool[i];
        }
    }
    return NULL;
}

struct netbuf *netbuf_alloc(void) {
    return netbuf_alloc_size(NETBUF_LARGE_SIZE);
}

void netbuf_ref(struct netbuf *buf) {
    if (buf && buf->refcount < 255)
        buf->refcount++;
}

void netbuf_free(struct netbuf *buf) {
    if (!buf)
        return;
    if (buf->refcount > 1) {
        buf->refcount--;
        return;
    }
    buf->refcount = 0;
    buf->in_use = false;
}
