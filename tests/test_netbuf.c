#include "test_main.h"
#include <string.h>
#include <stdbool.h>

/* Replicate netbuf pool logic for testing (avoids kernel header conflicts) */
#define NETBUF_SIZE     2048
#define NETBUF_POOL_SIZE 64

struct test_netbuf {
    uint8_t data[NETBUF_SIZE];
    uint32_t len;
    uint32_t offset;
    bool in_use;
};

static struct test_netbuf test_pool[NETBUF_POOL_SIZE];

static void test_netbuf_init(void) {
    memset(test_pool, 0, sizeof(test_pool));
}

static struct test_netbuf *test_netbuf_alloc(void) {
    for (int i = 0; i < NETBUF_POOL_SIZE; i++) {
        if (!test_pool[i].in_use) {
            test_pool[i].in_use = true;
            test_pool[i].len = 0;
            test_pool[i].offset = 0;
            return &test_pool[i];
        }
    }
    return NULL;
}

static void test_netbuf_free(struct test_netbuf *buf) {
    if (buf)
        buf->in_use = false;
}

void test_netbuf_alloc_free_basic(void) {
    test_netbuf_init();

    struct test_netbuf *b1 = test_netbuf_alloc();
    TEST("netbuf_alloc returns non-NULL", b1 != NULL);
    TEST("netbuf_alloc sets in_use", b1 && b1->in_use);
    TEST("netbuf_alloc sets len=0", b1 && b1->len == 0);

    test_netbuf_free(b1);
    TEST("netbuf_free clears in_use", b1 && !b1->in_use);

    struct test_netbuf *b2 = test_netbuf_alloc();
    TEST("netbuf reuse after free", b2 == b1);
    test_netbuf_free(b2);
}

void test_netbuf_exhaust_pool(void) {
    test_netbuf_init();

    struct test_netbuf *bufs[NETBUF_POOL_SIZE];
    for (int i = 0; i < NETBUF_POOL_SIZE; i++) {
        bufs[i] = test_netbuf_alloc();
    }
    TEST("netbuf pool fully allocated", bufs[NETBUF_POOL_SIZE - 1] != NULL);

    struct test_netbuf *overflow = test_netbuf_alloc();
    TEST("netbuf pool overflow returns NULL", overflow == NULL);

    test_netbuf_free(bufs[0]);
    struct test_netbuf *reuse = test_netbuf_alloc();
    TEST("netbuf realloc after free from exhausted pool", reuse == bufs[0]);

    for (int i = 0; i < NETBUF_POOL_SIZE; i++)
        test_netbuf_free(bufs[i]);
    test_netbuf_free(reuse);
}

void test_netbuf_suite(void) {
    printf("=== Netbuf pool tests ===\n");
    test_netbuf_alloc_free_basic();
    test_netbuf_exhaust_pool();
}
