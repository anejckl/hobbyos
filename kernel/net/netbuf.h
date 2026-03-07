#ifndef NETBUF_H
#define NETBUF_H

#include "../common.h"

#define NETBUF_SIZE     2048
#define NETBUF_POOL_SIZE 64

struct netbuf {
    uint8_t data[NETBUF_SIZE];
    uint32_t len;       /* actual data length */
    uint32_t offset;    /* start of payload within data[] */
    bool in_use;
};

void netbuf_init(void);
struct netbuf *netbuf_alloc(void);
void netbuf_free(struct netbuf *buf);

#endif /* NETBUF_H */
