/*
 * test_pmm.c — Unit tests for PMM bitmap functions.
 *
 * Includes the actual kernel pmm.c with stubs, but manually initializes
 * bitmap state (bypassing pmm_init which needs multiboot2 data).
 */

/*
 * Block ALL kernel headers FIRST — before any system includes.
 * This prevents kernel/common.h type conflicts with host types.
 */
#define COMMON_H
#define PMM_H
#define STRING_H
#define VGA_H
#define DEBUG_H

/* Provide types and hardware stubs */
#include "stubs.h"

/* System headers for test harness */
#include "test_main.h"
#include <string.h>

/* Stub out functions that pmm.c calls */
static void vga_printf(const char *fmt, ...) __attribute__((unused));
static void vga_printf(const char *fmt, ...) { (void)fmt; }
static void debug_printf(const char *fmt, ...) { (void)fmt; }

/* Linker symbol that pmm.c declares as extern char[] */
char _kernel_phys_end[4096];

/* Include the actual kernel PMM implementation */
#include "../kernel/memory/pmm.c"

/* ---- Test helpers ---- */

/*
 * Manually set up the PMM bitmap for testing.
 * Bypasses pmm_init() which requires multiboot2 info.
 */
static uint8_t test_bitmap_storage[4096];
static uint8_t test_refcount_storage[32768];

void pmm_test_setup(uint64_t num_pages) {
    bitmap = test_bitmap_storage;
    page_refcount = test_refcount_storage;
    total_pages = num_pages;
    bitmap_size = (num_pages + 7) / 8;
    free_pages = 0;
    max_phys_addr = num_pages * PAGE_SIZE;

    /* Mark all pages as used */
    memset(bitmap, 0xFF, bitmap_size);
    memset(page_refcount, 0, num_pages);
}

void pmm_test_free_range(uint64_t start_page, uint64_t count) {
    for (uint64_t i = start_page; i < start_page + count && i < total_pages; i++) {
        if (bitmap_test(i)) {
            bitmap_clear(i);
            free_pages++;
        }
    }
}

/* ---- Tests ---- */

static void test_bitmap_set_clear(void) {
    pmm_test_setup(256);

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

    /* Bit 63 */
    bitmap_clear(63);
    TEST("bitmap clear 63", !bitmap_test(63));
    bitmap_set(63);
    TEST("bitmap set 63", bitmap_test(63));
}

static void test_alloc_free(void) {
    pmm_test_setup(256);
    pmm_test_free_range(10, 10);
    TEST("free pages after setup", pmm_get_free_pages() == 10);

    uint64_t addr1 = pmm_alloc_page();
    TEST("alloc first = page 10", addr1 == 10 * PAGE_SIZE);
    TEST("free count after alloc1", pmm_get_free_pages() == 9);

    uint64_t addr2 = pmm_alloc_page();
    TEST("alloc second = page 11", addr2 == 11 * PAGE_SIZE);
    TEST("free count after alloc2", pmm_get_free_pages() == 8);

    pmm_free_page(addr1);
    TEST("free count after free", pmm_get_free_pages() == 9);
    uint64_t addr3 = pmm_alloc_page();
    TEST("realloc returns freed page", addr3 == addr1);
}

static void test_alloc_exhaustion(void) {
    pmm_test_setup(16);
    pmm_test_free_range(5, 2);
    TEST("2 free pages", pmm_get_free_pages() == 2);

    uint64_t a1 = pmm_alloc_page();
    uint64_t a2 = pmm_alloc_page();
    TEST("alloc page 5", a1 == 5 * PAGE_SIZE);
    TEST("alloc page 6", a2 == 6 * PAGE_SIZE);

    uint64_t a3 = pmm_alloc_page();
    TEST("alloc exhausted returns 0", a3 == 0);
}

static void test_free_idempotent(void) {
    pmm_test_setup(64);
    pmm_test_free_range(0, 4);
    TEST("4 free pages", pmm_get_free_pages() == 4);

    pmm_alloc_page();
    TEST("3 free after alloc", pmm_get_free_pages() == 3);

    /* Free a page that's already free — should be no-op */
    pmm_free_page(1 * PAGE_SIZE);
    TEST("free idempotent", pmm_get_free_pages() == 3);
}

static void test_total_pages(void) {
    pmm_test_setup(128);
    TEST("total pages 128", pmm_get_total_pages() == 128);

    pmm_test_setup(1024);
    TEST("total pages 1024", pmm_get_total_pages() == 1024);
}

static void test_free_out_of_bounds(void) {
    pmm_test_setup(16);
    pmm_test_free_range(0, 4);
    uint64_t before = pmm_get_free_pages();

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
