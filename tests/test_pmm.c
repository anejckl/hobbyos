/*
 * test_pmm.c — Unit tests for PMM bitmap functions.
 *
 * We include the kernel's pmm.c directly with stubs, but only test the
 * bitmap_set/clear/test helpers and pmm_alloc_page/pmm_free_page.
 * pmm_init() requires multiboot2 data so we set up state manually.
 */

#include "test_main.h"
#include <string.h>

/* Block kernel/common.h — stubs.h provides types */
#define COMMON_H
#include "stubs.h"

/* Provide headers that pmm.c includes */
#define PMM_H
#define STRING_H
#define VGA_H
#define DEBUG_H

/* Stub out functions pmm.c calls but we don't need for bitmap tests */
static void vga_printf(const char *fmt, ...) { (void)fmt; }
static void debug_printf(const char *fmt, ...) { (void)fmt; }

/* Provide the linker symbol pmm.c references */
static char _kernel_phys_end_storage[1];
char *_kernel_phys_end = _kernel_phys_end_storage;

/* Now include the actual pmm.c — we get bitmap_set/clear/test and
   pmm_alloc_page/pmm_free_page as real kernel code */
#include "../kernel/memory/pmm.c"

/* ---- Test helpers ---- */

/* Manually initialize the PMM bitmap for testing (bypasses pmm_init
   which needs multiboot2 info) */
static uint8_t test_bitmap_storage[4096];

static void pmm_test_setup(uint64_t num_pages) {
    bitmap = test_bitmap_storage;
    total_pages = num_pages;
    bitmap_size = (num_pages + 7) / 8;
    free_pages = 0;
    max_phys_addr = num_pages * PAGE_SIZE;

    /* Mark all pages as used */
    memset(bitmap, 0xFF, bitmap_size);
}

static void pmm_test_free_range(uint64_t start_page, uint64_t count) {
    for (uint64_t i = start_page; i < start_page + count && i < total_pages; i++) {
        if (bitmap_test(i)) {
            bitmap_clear(i);
            free_pages++;
        }
    }
}

/* ---- Tests ---- */

void test_bitmap_set_clear(void) {
    pmm_test_setup(256);
    /* Start with all bits set (all used) — clear some to test */

    /* Clear and test individual bits */
    bitmap_clear(0);
    TEST("bitmap clear 0", !bitmap_test(0));
    TEST("bitmap 1 still set", bitmap_test(1));

    bitmap_clear(7);
    TEST("bitmap clear 7", !bitmap_test(7));

    bitmap_clear(8);
    TEST("bitmap clear 8 (byte boundary)", !bitmap_test(8));

    /* Re-set bit 0 */
    bitmap_set(0);
    TEST("bitmap re-set 0", bitmap_test(0));
    TEST("bitmap 7 still clear", !bitmap_test(7));
    TEST("bitmap 8 still clear", !bitmap_test(8));

    /* Bit 63 (crosses multiple bytes) */
    bitmap_clear(63);
    TEST("bitmap clear 63", !bitmap_test(63));
    bitmap_set(63);
    TEST("bitmap set 63", bitmap_test(63));
}

void test_alloc_free(void) {
    pmm_test_setup(256);
    /* Free pages 10-19 (10 pages) */
    pmm_test_free_range(10, 10);
    TEST("free pages after setup", pmm_get_free_pages() == 10);

    /* First alloc should return page 10 (lowest free) */
    uint64_t addr1 = pmm_alloc_page();
    TEST("alloc first = page 10", addr1 == 10 * PAGE_SIZE);
    TEST("free count after alloc1", pmm_get_free_pages() == 9);

    /* Second alloc should return page 11 */
    uint64_t addr2 = pmm_alloc_page();
    TEST("alloc second = page 11", addr2 == 11 * PAGE_SIZE);
    TEST("free count after alloc2", pmm_get_free_pages() == 8);

    /* Free page 10 and re-alloc — should get it back (lowest free) */
    pmm_free_page(addr1);
    TEST("free count after free", pmm_get_free_pages() == 9);
    uint64_t addr3 = pmm_alloc_page();
    TEST("realloc returns freed page", addr3 == addr1);
}

void test_alloc_exhaustion(void) {
    pmm_test_setup(16);
    /* Free only 2 pages */
    pmm_test_free_range(5, 2);
    TEST("2 free pages", pmm_get_free_pages() == 2);

    uint64_t a1 = pmm_alloc_page();
    uint64_t a2 = pmm_alloc_page();
    TEST("alloc page 5", a1 == 5 * PAGE_SIZE);
    TEST("alloc page 6", a2 == 6 * PAGE_SIZE);

    /* Now exhausted — should return 0 */
    uint64_t a3 = pmm_alloc_page();
    TEST("alloc exhausted returns 0", a3 == 0);
}

void test_free_idempotent(void) {
    pmm_test_setup(64);
    pmm_test_free_range(0, 4);
    TEST("4 free pages", pmm_get_free_pages() == 4);

    /* Allocate one page (page 0) */
    pmm_alloc_page();
    TEST("3 free after alloc", pmm_get_free_pages() == 3);

    /* Free a page that's already free — should be no-op */
    pmm_free_page(1 * PAGE_SIZE);  /* page 1 is already free */
    TEST("free idempotent", pmm_get_free_pages() == 3);
}

void test_total_pages(void) {
    pmm_test_setup(128);
    TEST("total pages", pmm_get_total_pages() == 128);

    pmm_test_setup(1024);
    TEST("total pages 1024", pmm_get_total_pages() == 1024);
}

void test_free_out_of_bounds(void) {
    pmm_test_setup(16);
    pmm_test_free_range(0, 4);
    uint64_t before = pmm_get_free_pages();

    /* Free address beyond total pages — should be no-op */
    pmm_free_page(100 * PAGE_SIZE);
    TEST("free out of bounds no-op", pmm_get_free_pages() == before);
}

/* ---- Suite entry point ---- */

void test_pmm_suite(void) {
    printf("=== PMM bitmap tests ===\n");
    test_bitmap_set_clear();
    test_alloc_free();
    test_alloc_exhaustion();
    test_free_idempotent();
    test_total_pages();
    test_free_out_of_bounds();
}
