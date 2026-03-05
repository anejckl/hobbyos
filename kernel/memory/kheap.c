#include "kheap.h"
#include "pmm.h"
#include "vmm.h"
#include "../debug/debug.h"

/* Bump allocator */
#define KHEAP_START 0xFFFFFFFF90000000ULL
#define KHEAP_SIZE  (4ULL * 1024 * 1024)  /* 4MB initial heap */

static uint64_t heap_start;
static uint64_t heap_current;
static uint64_t heap_end;

void kheap_init(void) {
    heap_start = KHEAP_START;
    heap_current = KHEAP_START;
    heap_end = KHEAP_START + KHEAP_SIZE;

    /* Map heap pages */
    for (uint64_t addr = heap_start; addr < heap_end; addr += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0) {
            /* Reduce heap to what we could allocate */
            heap_end = addr;
            break;
        }
        vmm_map_page(addr, phys, PTE_WRITABLE);
    }

    debug_printf("KHEAP: 0x%x - 0x%x (%u KB)\n",
                 heap_start, heap_end, (heap_end - heap_start) / 1024);
}

void *kmalloc(size_t size) {
    /* 16-byte alignment */
    size = (size + 15) & ~15;

    if (heap_current + size > heap_end)
        return NULL;

    void *ptr = (void *)heap_current;
    heap_current += size;
    return ptr;
}

void kfree(void *ptr) {
    /* Bump allocator: kfree is a no-op */
    (void)ptr;
}
