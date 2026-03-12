#ifndef PAGECACHE_H
#define PAGECACHE_H

#include "../common.h"

#define PAGECACHE_MAX  512  /* 2 MB cache */

struct pagecache_entry {
    uint32_t inode;        /* ext2 inode (0 = unused) */
    uint64_t offset;       /* page-aligned file offset */
    uint64_t phys_page;    /* cached physical page */
    bool dirty;
    bool in_use;
    uint32_t access_tick;  /* LRU */
};

/* Initialize page cache */
void pagecache_init(void);

/* Get a cached page for (inode, offset). Reads from disk if not cached.
 * Returns physical address of cached page, or 0 on failure. */
uint64_t pagecache_get(uint32_t inode, uint64_t offset);

/* Mark a cached page as dirty */
void pagecache_mark_dirty(uint32_t inode, uint64_t offset);

/* Write all dirty pages to disk */
void pagecache_sync(void);

/* Evict up to 'count' pages (LRU, clean first) */
int pagecache_evict(int count);

/* Invalidate all entries for an inode */
void pagecache_invalidate(uint32_t inode);

#endif /* PAGECACHE_H */
