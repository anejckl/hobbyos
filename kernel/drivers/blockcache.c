#include "blockcache.h"
#include "ata.h"
#include "../memory/kheap.h"
#include "../drivers/pit.h"
#include "../string.h"
#include "../debug/debug.h"

static struct bcache_entry cache[BCACHE_SIZE];
static struct bcache_entry *hash_table[BCACHE_HASH_SIZE];
static bool bcache_ready = false;
static uint64_t cache_hits = 0;
static uint64_t cache_misses = 0;

/* Current ext2 block size — set by bcache_init caller */
static uint32_t cached_block_size = 1024;

static uint32_t hash_fn(uint32_t block_no) {
    return block_no % BCACHE_HASH_SIZE;
}

void bcache_init(void) {
    memset(cache, 0, sizeof(cache));
    memset(hash_table, 0, sizeof(hash_table));

    /* Allocate data buffers for each cache entry */
    for (int i = 0; i < BCACHE_SIZE; i++) {
        cache[i].data = (uint8_t *)kmalloc(4096);
        if (!cache[i].data) {
            debug_printf("bcache: only allocated %d buffers\n", (int64_t)i);
            break;
        }
    }

    bcache_ready = true;
    debug_printf("bcache: initialized with %d entries\n", (int64_t)BCACHE_SIZE);
}

void bcache_set_block_size(uint32_t bs) {
    cached_block_size = bs;
}

/* Read a block from disk into buffer */
static int bcache_disk_read(uint32_t block_no, uint8_t *buf) {
    uint32_t sectors_per_block = cached_block_size / 512;
    uint32_t lba = block_no * sectors_per_block;
    return ata_read_sectors(lba, (uint8_t)sectors_per_block, buf);
}

/* Write a block from buffer to disk */
static int bcache_disk_write(uint32_t block_no, const uint8_t *buf) {
    uint32_t sectors_per_block = cached_block_size / 512;
    uint32_t lba = block_no * sectors_per_block;
    return ata_write_sectors(lba, (uint8_t)sectors_per_block, buf);
}

/* Find entry in hash chain */
static struct bcache_entry *bcache_hash_find(uint32_t block_no) {
    uint32_t h = hash_fn(block_no);
    struct bcache_entry *e = hash_table[h];
    while (e) {
        if (e->valid && e->block_no == block_no)
            return e;
        e = e->hash_next;
    }
    return NULL;
}

/* Remove entry from hash chain */
static void bcache_hash_remove(struct bcache_entry *entry) {
    uint32_t h = hash_fn(entry->block_no);
    struct bcache_entry **pp = &hash_table[h];
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->hash_next;
            entry->hash_next = NULL;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/* Insert entry into hash chain */
static void bcache_hash_insert(struct bcache_entry *entry) {
    uint32_t h = hash_fn(entry->block_no);
    entry->hash_next = hash_table[h];
    hash_table[h] = entry;
}

/* Find an evictable cache slot (LRU among unpinned, clean entries) */
static struct bcache_entry *bcache_evict(void) {
    struct bcache_entry *best = NULL;
    uint32_t best_tick = 0xFFFFFFFF;

    /* First pass: find unpinned clean entry with oldest tick */
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (!cache[i].data)
            continue;
        if (!cache[i].valid) {
            /* Unused slot — use immediately */
            return &cache[i];
        }
        if (cache[i].refcount == 0 && !cache[i].dirty) {
            if (cache[i].lru_tick < best_tick) {
                best_tick = cache[i].lru_tick;
                best = &cache[i];
            }
        }
    }

    if (best) {
        bcache_hash_remove(best);
        best->valid = false;
        return best;
    }

    /* Second pass: evict dirty unpinned entries (write back first) */
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (!cache[i].data || !cache[i].valid)
            continue;
        if (cache[i].refcount == 0) {
            if (cache[i].lru_tick < best_tick) {
                best_tick = cache[i].lru_tick;
                best = &cache[i];
            }
        }
    }

    if (best) {
        if (best->dirty) {
            bcache_disk_write(best->block_no, best->data);
            best->dirty = false;
        }
        bcache_hash_remove(best);
        best->valid = false;
        return best;
    }

    return NULL; /* All entries pinned */
}

struct bcache_entry *bcache_get(uint32_t block_no) {
    if (!bcache_ready)
        return NULL;

    /* Check hash table first */
    struct bcache_entry *e = bcache_hash_find(block_no);
    if (e) {
        e->refcount++;
        e->lru_tick = (uint32_t)pit_get_ticks();
        cache_hits++;
        return e;
    }

    /* Cache miss — need to load from disk */
    cache_misses++;

    e = bcache_evict();
    if (!e)
        return NULL;

    e->block_no = block_no;
    e->dirty = false;
    e->refcount = 1;
    e->lru_tick = (uint32_t)pit_get_ticks();

    if (bcache_disk_read(block_no, e->data) < 0) {
        e->valid = false;
        return NULL;
    }

    e->valid = true;
    e->in_use = true;
    bcache_hash_insert(e);
    return e;
}

void bcache_release(struct bcache_entry *entry) {
    if (entry && entry->refcount > 0)
        entry->refcount--;
}

void bcache_mark_dirty(struct bcache_entry *entry) {
    if (entry)
        entry->dirty = true;
}

void bcache_sync(void) {
    if (!bcache_ready)
        return;

    int flushed = 0;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].dirty) {
            bcache_disk_write(cache[i].block_no, cache[i].data);
            cache[i].dirty = false;
            flushed++;
        }
    }

    if (flushed > 0)
        debug_printf("bcache: synced %d dirty blocks\n", (int64_t)flushed);
}

uint64_t bcache_get_hits(void) {
    return cache_hits;
}

uint64_t bcache_get_misses(void) {
    return cache_misses;
}
