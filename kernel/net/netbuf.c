#include "netbuf.h"
#include "../string.h"
#include "../debug/debug.h"

static struct netbuf pool[NETBUF_POOL_SIZE];

void netbuf_init(void) {
    memset(pool, 0, sizeof(pool));
    debug_printf("netbuf: pool initialized (%u buffers, %u bytes each)\n",
                 (uint64_t)NETBUF_POOL_SIZE, (uint64_t)NETBUF_SIZE);
}

struct netbuf *netbuf_alloc(void) {
    for (int i = 0; i < NETBUF_POOL_SIZE; i++) {
        if (!pool[i].in_use) {
            pool[i].in_use = true;
            pool[i].len = 0;
            pool[i].offset = 0;
            return &pool[i];
        }
    }
    return NULL;
}

void netbuf_free(struct netbuf *buf) {
    if (buf)
        buf->in_use = false;
}
