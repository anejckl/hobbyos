/*
 * test_swap.c — Host-side tests for swap table alloc/free logic
 */
#include "test_main.h"
#include <string.h>

#define TEST_SWAP_SLOTS 16

static int swap_slots[TEST_SWAP_SLOTS];
static int swap_used = 0;

static void test_swap_init(void) {
    memset(swap_slots, 0, sizeof(swap_slots));
    swap_used = 0;
}

static int test_swap_alloc(void) {
    for (int i = 0; i < TEST_SWAP_SLOTS; i++) {
        if (!swap_slots[i]) {
            swap_slots[i] = 1;
            swap_used++;
            return i;
        }
    }
    return -1;
}

static void test_swap_free(int slot) {
    if (slot >= 0 && slot < TEST_SWAP_SLOTS && swap_slots[slot]) {
        swap_slots[slot] = 0;
        swap_used--;
    }
}

void test_swap_basic(void) {
    test_swap_init();

    /* Allocate first slot */
    int s1 = test_swap_alloc();
    TEST("swap alloc first", s1 == 0);
    TEST("swap used count 1", swap_used == 1);

    int s2 = test_swap_alloc();
    TEST("swap alloc second", s2 == 1);
    TEST("swap used count 2", swap_used == 2);

    /* Free first slot */
    test_swap_free(s1);
    TEST("swap free count", swap_used == 1);

    /* Re-allocate should reuse slot 0 */
    int s3 = test_swap_alloc();
    TEST("swap reuse freed slot", s3 == 0);
}

void test_swap_full(void) {
    test_swap_init();

    /* Fill all slots */
    for (int i = 0; i < TEST_SWAP_SLOTS; i++) {
        int s = test_swap_alloc();
        TEST("swap alloc fills", s == i);
    }
    TEST("swap full count", swap_used == TEST_SWAP_SLOTS);

    /* Should fail when full */
    int s = test_swap_alloc();
    TEST("swap alloc fails when full", s == -1);

    /* Free one and re-allocate */
    test_swap_free(5);
    s = test_swap_alloc();
    TEST("swap alloc after free", s == 5);
}

void test_swap_suite(void) {
    printf("=== Swap table tests ===\n");
    test_swap_basic();
    test_swap_full();
}
