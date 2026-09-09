/*
 * test_kheap.c — Unit tests for the kernel heap allocator (kernel/memory/kheap.c).
 *
 * Includes the actual kernel kheap.c with stubs, then manually initializes
 * the free list over a host-memory buffer (bypassing kheap_init(), which
 * needs real PMM/VMM state), the same pattern test_pmm.c uses for pmm.c.
 *
 * Exists to verify, empirically, whether kmalloc_page_aligned() can fail
 * under external fragmentation despite abundant total free memory — the
 * real mechanism behind the "sh: fork failed" CI bug fixed in
 * kernel/process/process.c (commit 5815234), which was originally
 * (incorrectly) attributed to kfree() being a no-op. kheap.c is a real
 * first-fit free-list allocator with forward+backward coalescing — kfree()
 * genuinely reclaims memory. The actual failure mode this test targets is
 * page-aligned allocation failing to find one *contiguous* free run even
 * though the *sum* of free bytes is far larger than the request.
 */

#define COMMON_H
#define KHEAP_H
#define PMM_H
#define VMM_H
#define STRING_H
#define DEBUG_H

#include "stubs.h"
#include "test_main.h"
#include <string.h>

static void debug_printf(const char *fmt, ...) { (void)fmt; }

/* kheap_init() (never called by these tests) references this flag */
#define PTE_WRITABLE 0x2ULL

/* kheap.c calls these; never exercised by these tests (kheap_init() is
 * bypassed in favor of manually seeding the free list over host memory,
 * same as test_pmm.c bypasses pmm_init()). static: test_pmm.c's own
 * translation unit already defines the real (non-static) pmm_alloc_page
 * by including the actual kernel pmm.c — internal linkage here avoids a
 * duplicate-symbol link error against that. */
static uint64_t pmm_alloc_page(void) { return 0; }
static void vmm_map_page(uint64_t vaddr, uint64_t phys, uint64_t flags) {
    (void)vaddr; (void)phys; (void)flags;
}

#include "../kernel/memory/kheap.c"

/* ---- Test helpers ---- */

static uint8_t host_heap[KHEAP_SIZE] __attribute__((aligned(4096)));

static void reset_heap(void) {
    heap_start = (uint64_t)(uintptr_t)host_heap;
    heap_end = heap_start + KHEAP_SIZE;
    free_list = (struct kheap_block *)(uintptr_t)host_heap;
    free_list->size = KHEAP_SIZE - BLOCK_HDR_SIZE;
    free_list->next = NULL;
    free_list->magic = BLOCK_MAGIC_FREE;
}

static uint64_t total_free_bytes(void) {
    uint64_t total = 0;
    for (struct kheap_block *b = free_list; b; b = b->next)
        total += b->size;
    return total;
}

/* ---- Tests ---- */

void test_kheap_basic_alloc_free(void) {
    reset_heap();
    void *a = kmalloc(64);
    TEST("kmalloc returns non-NULL", a != NULL);
    kfree(a);
    void *b = kmalloc(64);
    TEST("kfree'd block is reused (same address)", b == a);
    kfree(b);
}

void test_kheap_coalesce_forward_and_backward(void) {
    reset_heap();
    void *a = kmalloc(64);
    void *b = kmalloc(64);
    void *c = kmalloc(64);
    (void)a;
    /* Free b, then c: c should coalesce backward into b's freed block */
    kfree(b);
    kfree(c);
    /* Now free a: should coalesce forward into the merged b+c block,
     * producing a single free block spanning the whole heap again
     * (since a, b, c were carved sequentially from one initial block). */
    kfree(a);
    TEST("full coalescing restores a single free block", free_list->next == NULL);
    TEST("coalesced block reclaims all usable heap space",
         free_list->size == KHEAP_SIZE - BLOCK_HDR_SIZE);
}

void test_kheap_page_aligned_basic(void) {
    reset_heap();
    void *p = kmalloc_page_aligned(20480);
    TEST("kmalloc_page_aligned returns non-NULL on fresh heap", p != NULL);
    TEST("kmalloc_page_aligned result is page-aligned",
         ((uintptr_t)p & (PAGE_SIZE - 1)) == 0);
    kfree(p);
}

/* Regression test for a real bug found via this exact stress pattern:
 * kmalloc_page_aligned() only handled a candidate block's alignment
 * "waste" (padding before the page boundary) when it was exactly 0 or
 * >= BLOCK_HDR_SIZE + MIN_BLOCK_SIZE (40) — a waste of 1..39 bytes (which
 * depends only on a block's start address modulo PAGE_SIZE, nothing
 * about its size) fell through neither branch and rejected the block
 * outright, even when it was the *only* free block in the entire heap
 * and vastly bigger than needed. Reproduced with a single, unfragmented
 * ~4MB free block: failed at cycle 170/200 with 4,132,880 of 4,194,304
 * bytes still free, needing only 20,480 — not genuine fragmentation
 * (confirmed: only 1 free block existed at failure), a straightforward
 * algorithm gap. Fixed by bumping to the next page boundary when the
 * natural waste is in that dead zone (always >= PAGE_SIZE there, which
 * is comfortably enough to be its own free block). This test interleaves
 * small "permanent" allocations (simulating long-lived kernel structures
 * — VFS nodes, ext2 buffers, network buffers) with alloc/free cycles of
 * a kernel-stack-sized page-aligned block, and asserts zero failures. */
void test_kheap_page_aligned_fragmentation(void) {
    reset_heap();

    #define STACK_SIZE 20480
    #define CYCLES 200

    int page_aligned_failures = 0;
    uint64_t first_failure_cycle = 0;
    uint64_t free_bytes_at_first_failure = 0;

    for (int i = 0; i < CYCLES; i++) {
        /* A few small "permanent" allocations of varying, non-power-of-2
         * sizes — never freed, simulating persistent kernel churn. */
        kmalloc(48 + (uint64_t)(i % 7) * 16);
        kmalloc(96 + (uint64_t)(i % 5) * 32);
        kmalloc(24);

        void *stack = kmalloc_page_aligned(STACK_SIZE);
        if (!stack) {
            if (page_aligned_failures == 0) {
                first_failure_cycle = (uint64_t)i;
                free_bytes_at_first_failure = total_free_bytes();

                uint64_t largest = 0;
                int nblocks = 0;
                for (struct kheap_block *b = free_list; b; b = b->next) {
                    nblocks++;
                    if (b->size > largest) largest = b->size;
                    uint64_t ds = (uint64_t)b + BLOCK_HDR_SIZE;
                    uint64_t al = (ds + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                    uint64_t w = al - ds;
                    if (b->size >= w + STACK_SIZE) {
                        printf("  (DIAGNOSTIC: block size=%u waste=%u WOULD fit "
                               "%u+%u=%u but was rejected!)\n",
                               (unsigned)b->size, (unsigned)w,
                               (unsigned)w, (unsigned)STACK_SIZE,
                               (unsigned)(w + STACK_SIZE));
                    }
                }
                printf("  (DIAGNOSTIC: %d free blocks, largest=%u bytes, "
                       "needed %u contiguous)\n",
                       nblocks, (unsigned)largest, (unsigned)STACK_SIZE);
            }
            page_aligned_failures++;
            continue;
        }
        kfree(stack);
    }

    if (page_aligned_failures > 0) {
        printf("  (REGRESSION: first failure at cycle %u, %u/%u total, "
               "%u bytes still free at failure, needed %u)\n",
               (unsigned)first_failure_cycle, (unsigned)page_aligned_failures,
               (unsigned)CYCLES, (unsigned)free_bytes_at_first_failure,
               (unsigned)STACK_SIZE);
    }

    TEST("kmalloc_page_aligned never fails under this stress pattern "
         "(alignment-waste dead zone fixed; would have failed at cycle "
         "170/200 before the fix)",
         page_aligned_failures == 0);

    #undef STACK_SIZE
    #undef CYCLES
}

void test_kheap_suite(void) {
    printf("=== kheap tests ===\n");
    test_kheap_basic_alloc_free();
    test_kheap_coalesce_forward_and_backward();
    test_kheap_page_aligned_basic();
    test_kheap_page_aligned_fragmentation();
}
