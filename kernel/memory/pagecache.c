#include "pagecache.h"
#include "pmm.h"
#include "../drivers/pit.h"
#include "../drivers/ata.h"
#include "../fs/ext2.h"
#include "../string.h"
#include "../debug/debug.h"

static struct pagecache_entry cache[PAGECACHE_MAX];
static bool pagecache_ready = false;

void pagecache_init(void) {
    memset(cache, 0, sizeof(cache));
    pagecache_ready = true;
    debug_printf("pagecache: initialized with %u entries\n", (uint64_t)PAGECACHE_MAX);
}

/* Find entry for (inode, offset) */
static struct pagecache_entry *pagecache_find(uint32_t inode, uint64_t offset) {
    for (int i = 0; i < PAGECACHE_MAX; i++) {
        if (cache[i].in_use && cache[i].inode == inode && cache[i].offset == offset)
            return &cache[i];
    }
    return NULL;
}

/* Find a free or evictable slot */
static struct pagecache_entry *pagecache_alloc_entry(void) {
    /* First: find unused slot */
    for (int i = 0; i < PAGECACHE_MAX; i++) {
        if (!cache[i].in_use)
            return &cache[i];
    }

    /* LRU eviction: find oldest clean entry */
    struct pagecache_entry *best = NULL;
    uint32_t oldest = 0xFFFFFFFF;

    for (int i = 0; i < PAGECACHE_MAX; i++) {
        if (!cache[i].dirty && cache[i].access_tick < oldest) {
            oldest = cache[i].access_tick;
            best = &cache[i];
        }
    }

    if (best) {
        /* Free the physical page */
        if (best->phys_page)
            pmm_page_unref(best->phys_page);
        best->in_use = false;
        return best;
    }

    /* All dirty — evict oldest dirty entry (write back first) */
    for (int i = 0; i < PAGECACHE_MAX; i++) {
        if (cache[i].access_tick < oldest) {
            oldest = cache[i].access_tick;
            best = &cache[i];
        }
    }

    if (best && best->dirty) {
        /* Write back */
        struct ext2_inode inode;
        if (ext2_read_inode(best->inode, &inode) == 0) {
            ext2_write_file(best->inode, &inode, best->offset, PAGE_SIZE,
                           (uint8_t *)PHYS_TO_VIRT(best->phys_page));
        }
        best->dirty = false;
    }

    if (best) {
        if (best->phys_page)
            pmm_page_unref(best->phys_page);
        best->in_use = false;
        return best;
    }

    return NULL;
}

uint64_t pagecache_get(uint32_t inode, uint64_t offset) {
    if (!pagecache_ready)
        return 0;

    /* Page-align offset */
    offset &= ~(PAGE_SIZE - 1);

    /* Check cache first */
    struct pagecache_entry *e = pagecache_find(inode, offset);
    if (e) {
        e->access_tick = (uint32_t)pit_get_ticks();
        return e->phys_page;
    }

    /* Cache miss — allocate entry and load from disk */
    e = pagecache_alloc_entry();
    if (!e)
        return 0;

    uint64_t phys = pmm_alloc_page();
    if (!phys)
        return 0;

    memset((void *)PHYS_TO_VIRT(phys), 0, PAGE_SIZE);

    /* Read file data into page */
    struct ext2_inode inode_data;
    if (ext2_read_inode(inode, &inode_data) == 0) {
        ext2_read_file(&inode_data, offset, PAGE_SIZE,
                       (uint8_t *)PHYS_TO_VIRT(phys));
    }

    e->inode = inode;
    e->offset = offset;
    e->phys_page = phys;
    e->dirty = false;
    e->in_use = true;
    e->access_tick = (uint32_t)pit_get_ticks();

    return phys;
}

void pagecache_mark_dirty(uint32_t inode, uint64_t offset) {
    offset &= ~(PAGE_SIZE - 1);
    struct pagecache_entry *e = pagecache_find(inode, offset);
    if (e)
        e->dirty = true;
}

void pagecache_sync(void) {
    if (!pagecache_ready)
        return;

    int synced = 0;
    for (int i = 0; i < PAGECACHE_MAX; i++) {
        if (cache[i].in_use && cache[i].dirty) {
            struct ext2_inode inode;
            if (ext2_read_inode(cache[i].inode, &inode) == 0) {
                ext2_write_file(cache[i].inode, &inode, cache[i].offset,
                               PAGE_SIZE, (uint8_t *)PHYS_TO_VIRT(cache[i].phys_page));
                cache[i].dirty = false;
                synced++;
            }
        }
    }
    if (synced > 0)
        debug_printf("pagecache: synced %d dirty pages\n", (int64_t)synced);
}

int pagecache_evict(int count) {
    int evicted = 0;
    for (int pass = 0; pass < 2 && evicted < count; pass++) {
        uint32_t oldest = 0xFFFFFFFF;
        struct pagecache_entry *best = NULL;

        for (int i = 0; i < PAGECACHE_MAX; i++) {
            if (!cache[i].in_use)
                continue;
            /* Pass 0: clean only. Pass 1: any. */
            if (pass == 0 && cache[i].dirty)
                continue;
            if (cache[i].access_tick < oldest) {
                oldest = cache[i].access_tick;
                best = &cache[i];
            }
        }

        if (best) {
            if (best->dirty) {
                struct ext2_inode inode;
                if (ext2_read_inode(best->inode, &inode) == 0) {
                    ext2_write_file(best->inode, &inode, best->offset,
                                   PAGE_SIZE, (uint8_t *)PHYS_TO_VIRT(best->phys_page));
                }
            }
            if (best->phys_page)
                pmm_page_unref(best->phys_page);
            best->in_use = false;
            evicted++;
        }
    }
    return evicted;
}

void pagecache_invalidate(uint32_t inode) {
    for (int i = 0; i < PAGECACHE_MAX; i++) {
        if (cache[i].in_use && cache[i].inode == inode) {
            if (cache[i].phys_page)
                pmm_page_unref(cache[i].phys_page);
            cache[i].in_use = false;
        }
    }
}
