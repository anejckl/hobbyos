/*
 * test_refcount.c — Unit tests for PMM page reference counting.
 *
 * Re-uses the same include trick as test_pmm.c — pmm.c is already
 * included there, so we just call the pmm functions directly.
 * The pmm_test_setup() helper is defined in test_pmm.c (linked together).
 */

#include "test_main.h"

/* These are defined in test_pmm.c (which includes pmm.c) */
extern uint64_t pmm_alloc_page(void);
extern void pmm_free_page(uint64_t phys_addr);
extern uint64_t pmm_get_free_pages(void);
extern void pmm_page_ref(uint64_t phys_addr);
extern void pmm_page_unref(uint64_t phys_addr);
extern uint8_t pmm_page_refcount(uint64_t phys_addr);

/* Defined in test_pmm.c */
void pmm_test_setup(uint64_t num_pages);
void pmm_test_free_range(uint64_t start_page, uint64_t count);

#define PAGE_SIZE 4096

static void test_refcount_alloc(void) {
    pmm_test_setup(64);
    pmm_test_free_range(10, 4);

    uint64_t addr = pmm_alloc_page();
    TEST("alloc sets refcount to 1", pmm_page_refcount(addr) == 1);

    uint64_t addr2 = pmm_alloc_page();
    TEST("second alloc also refcount 1", pmm_page_refcount(addr2) == 1);
}

static void test_refcount_increment(void) {
    pmm_test_setup(64);
    pmm_test_free_range(10, 4);

    uint64_t addr = pmm_alloc_page();
    pmm_page_ref(addr);
    TEST("ref increments to 2", pmm_page_refcount(addr) == 2);

    pmm_page_ref(addr);
    TEST("ref increments to 3", pmm_page_refcount(addr) == 3);
}

static void test_refcount_decrement(void) {
    pmm_test_setup(64);
    pmm_test_free_range(10, 4);

    uint64_t addr = pmm_alloc_page();
    uint64_t free_before = pmm_get_free_pages();

    pmm_page_ref(addr);  /* refcount = 2 */
    pmm_page_unref(addr); /* refcount = 1 */
    TEST("unref decrements to 1", pmm_page_refcount(addr) == 1);
    TEST("page not freed at refcount 1", pmm_get_free_pages() == free_before);
}

static void test_refcount_free_on_zero(void) {
    pmm_test_setup(64);
    pmm_test_free_range(10, 4);

    uint64_t addr = pmm_alloc_page();
    uint64_t free_before = pmm_get_free_pages();

    pmm_page_unref(addr); /* refcount = 0 -> free */
    TEST("refcount is 0", pmm_page_refcount(addr) == 0);
    TEST("page freed when refcount hits 0", pmm_get_free_pages() == free_before + 1);
}

static void test_refcount_shared_page(void) {
    pmm_test_setup(64);
    pmm_test_free_range(10, 4);

    uint64_t addr = pmm_alloc_page();  /* refcount = 1 */
    pmm_page_ref(addr);                /* refcount = 2 (shared) */
    pmm_page_ref(addr);                /* refcount = 3 */

    uint64_t free_before = pmm_get_free_pages();

    pmm_page_unref(addr);  /* refcount = 2 */
    TEST("still allocated at refcount 2", pmm_get_free_pages() == free_before);

    pmm_page_unref(addr);  /* refcount = 1 */
    TEST("still allocated at refcount 1", pmm_get_free_pages() == free_before);

    pmm_page_unref(addr);  /* refcount = 0, freed */
    TEST("freed at refcount 0", pmm_get_free_pages() == free_before + 1);
}

void test_refcount_suite(void) {
    printf("=== PMM refcount tests ===\n");
    test_refcount_alloc();
    test_refcount_increment();
    test_refcount_decrement();
    test_refcount_free_on_zero();
    test_refcount_shared_page();
}
