#ifndef BLOCKCACHE_H
#define BLOCKCACHE_H

#include "../common.h"

#define BCACHE_SIZE       256   /* 256 cached blocks = 1 MB at 4096 block_size */
#define BCACHE_HASH_SIZE   64

struct bcache_entry {
    uint32_t block_no;
    uint8_t *data;               /* page-sized buffer */
    bool dirty;
    bool valid;
    bool in_use;
    uint32_t refcount;           /* pinned while caller holds reference */
    uint32_t lru_tick;
    struct bcache_entry *hash_next;
};

/* Initialize block cache (allocates buffers from kheap) */
void bcache_init(void);

/* Get a cached block. Reads from disk if not cached.
 * Returns entry with refcount incremented, or NULL on failure. */
struct bcache_entry *bcache_get(uint32_t block_no);

/* Release a block (decrements refcount). */
void bcache_release(struct bcache_entry *entry);

/* Mark a block as dirty (will be written on sync). */
void bcache_mark_dirty(struct bcache_entry *entry);

/* Write all dirty blocks to disk. */
void bcache_sync(void);

/* Get cache statistics */
uint64_t bcache_get_hits(void);
uint64_t bcache_get_misses(void);

#endif /* BLOCKCACHE_H */
