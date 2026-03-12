/*
 * test_bcache.c — Host-side tests for block cache hash/LRU logic
 */
#include "test_main.h"
#include <string.h>

/* Simplified bcache entry for host testing */
struct test_bcache_entry {
    uint32_t block_no;
    uint8_t data[64];   /* small for testing */
    int dirty;
    int valid;
    int in_use;
    uint32_t refcount;
    uint32_t lru_tick;
};

#define TEST_CACHE_SIZE 8
#define TEST_HASH_SIZE  4

static struct test_bcache_entry test_cache[TEST_CACHE_SIZE];

static uint32_t test_hash(uint32_t block_no) {
    return block_no % TEST_HASH_SIZE;
}

static void test_cache_init(void) {
    memset(test_cache, 0, sizeof(test_cache));
}

/* Find entry by block number */
static struct test_bcache_entry *test_cache_find(uint32_t block_no) {
    for (int i = 0; i < TEST_CACHE_SIZE; i++) {
        if (test_cache[i].valid && test_cache[i].block_no == block_no)
            return &test_cache[i];
    }
    return NULL;
}

/* Find free or LRU evictable slot */
static struct test_bcache_entry *test_cache_evict(void) {
    /* Free slot */
    for (int i = 0; i < TEST_CACHE_SIZE; i++) {
        if (!test_cache[i].valid)
            return &test_cache[i];
    }
    /* LRU clean */
    struct test_bcache_entry *best = NULL;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < TEST_CACHE_SIZE; i++) {
        if (test_cache[i].refcount == 0 && !test_cache[i].dirty &&
            test_cache[i].lru_tick < oldest) {
            oldest = test_cache[i].lru_tick;
            best = &test_cache[i];
        }
    }
    return best;
}

void test_bcache_basic(void) {
    test_cache_init();

    /* Add entries */
    for (int i = 0; i < TEST_CACHE_SIZE; i++) {
        test_cache[i].block_no = (uint32_t)i;
        test_cache[i].valid = 1;
        test_cache[i].lru_tick = (uint32_t)i;
    }

    TEST("bcache find existing", test_cache_find(3) != NULL);
    TEST("bcache find existing correct", test_cache_find(3)->block_no == 3);
    TEST("bcache find non-existing", test_cache_find(100) == NULL);
    TEST("bcache hash function", test_hash(0) == 0);
    TEST("bcache hash function 2", test_hash(5) == 1);
}

void test_bcache_eviction(void) {
    test_cache_init();

    /* Fill cache */
    for (int i = 0; i < TEST_CACHE_SIZE; i++) {
        test_cache[i].block_no = (uint32_t)i;
        test_cache[i].valid = 1;
        test_cache[i].lru_tick = (uint32_t)(i * 10);
        test_cache[i].refcount = 0;
        test_cache[i].dirty = 0;
    }

    /* Eviction should return oldest (tick=0) */
    struct test_bcache_entry *e = test_cache_evict();
    TEST("bcache evict returns LRU", e != NULL);
    TEST("bcache evict returns oldest", e && e->block_no == 0);

    /* Pin entry 0, evict again should return entry 1 */
    test_cache[0].refcount = 1;
    e = test_cache_evict();
    TEST("bcache evict skips pinned", e != NULL && e->block_no == 1);

    /* Mark entry 1 dirty, evict should return entry 2 */
    test_cache[1].dirty = 1;
    e = test_cache_evict();
    TEST("bcache evict skips dirty", e != NULL && e->block_no == 2);
}

void test_bcache_suite(void) {
    printf("=== Block cache tests ===\n");
    test_bcache_basic();
    test_bcache_eviction();
}
