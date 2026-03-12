#include "kheap.h"
#include "pmm.h"
#include "vmm.h"
#include "../debug/debug.h"
#include "../string.h"

/* Free-list allocator */
#define KHEAP_START 0xFFFFFFFF90000000ULL
#define KHEAP_SIZE  (4ULL * 1024 * 1024)  /* 4MB initial heap */

/* Block header: placed before every allocation AND every free chunk */
struct kheap_block {
    uint64_t size;              /* usable size (excluding header) */
    struct kheap_block *next;   /* next free block (only valid if free) */
    uint64_t magic;             /* 0xDEADBEEF if allocated, 0xFREEBEEF if free */
};

#define BLOCK_MAGIC_ALLOC  0xDEADBEEFULL
#define BLOCK_MAGIC_FREE   0xFBEEBEEFULL
#define BLOCK_HDR_SIZE     sizeof(struct kheap_block)
#define MIN_BLOCK_SIZE     16  /* minimum usable size per block */

static struct kheap_block *free_list;
static uint64_t heap_start;
static uint64_t heap_end;

void kheap_init(void) {
    heap_start = KHEAP_START;
    heap_end = KHEAP_START + KHEAP_SIZE;

    /* Map heap pages */
    uint64_t mapped_end = heap_start;
    for (uint64_t addr = heap_start; addr < heap_end; addr += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0) {
            heap_end = addr;
            break;
        }
        vmm_map_page(addr, phys, PTE_WRITABLE);
        mapped_end = addr + PAGE_SIZE;
    }
    heap_end = mapped_end;

    /* Initialize single free block spanning entire heap */
    free_list = (struct kheap_block *)heap_start;
    free_list->size = (heap_end - heap_start) - BLOCK_HDR_SIZE;
    free_list->next = NULL;
    free_list->magic = BLOCK_MAGIC_FREE;

    debug_printf("KHEAP: 0x%x - 0x%x (%u KB) [free-list]\n",
                 heap_start, heap_end, (heap_end - heap_start) / 1024);
}

void *kmalloc(size_t size) {
    if (size == 0)
        return NULL;

    /* 16-byte alignment */
    size = (size + 15) & ~15ULL;

    /* First-fit walk of free list */
    struct kheap_block *prev = NULL;
    struct kheap_block *cur = free_list;

    while (cur) {
        if (cur->magic != BLOCK_MAGIC_FREE) {
            /* Corrupted free list — bail out */
            debug_printf("KHEAP: corrupted free list at 0x%x\n", (uint64_t)cur);
            return NULL;
        }

        if (cur->size >= size) {
            /* Can we split this block? */
            if (cur->size >= size + BLOCK_HDR_SIZE + MIN_BLOCK_SIZE) {
                /* Split: create new free block after this allocation */
                struct kheap_block *new_free = (struct kheap_block *)
                    ((uint8_t *)cur + BLOCK_HDR_SIZE + size);
                new_free->size = cur->size - size - BLOCK_HDR_SIZE;
                new_free->next = cur->next;
                new_free->magic = BLOCK_MAGIC_FREE;

                cur->size = size;
                cur->next = NULL;
                cur->magic = BLOCK_MAGIC_ALLOC;

                /* Update free list */
                if (prev)
                    prev->next = new_free;
                else
                    free_list = new_free;
            } else {
                /* Use entire block */
                cur->magic = BLOCK_MAGIC_ALLOC;
                if (prev)
                    prev->next = cur->next;
                else
                    free_list = cur->next;
                cur->next = NULL;
            }

            return (void *)((uint8_t *)cur + BLOCK_HDR_SIZE);
        }

        prev = cur;
        cur = cur->next;
    }

    return NULL;  /* Out of memory */
}

void *kmalloc_page_aligned(size_t size) {
    if (size == 0)
        return NULL;

    /* Round size up to page boundary */
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /* Walk free list looking for a block we can carve a page-aligned region from */
    struct kheap_block *prev = NULL;
    struct kheap_block *cur = free_list;

    while (cur) {
        if (cur->magic != BLOCK_MAGIC_FREE)
            return NULL;

        uint64_t data_start = (uint64_t)cur + BLOCK_HDR_SIZE;
        uint64_t aligned = (data_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t waste = aligned - data_start;

        /* We need: waste bytes padding + size bytes for user data
         * The block must be able to hold a header at 'aligned - BLOCK_HDR_SIZE' */
        if (waste == 0 && cur->size >= size) {
            /* Already aligned — treat as normal allocation */
            if (cur->size >= size + BLOCK_HDR_SIZE + MIN_BLOCK_SIZE) {
                struct kheap_block *new_free = (struct kheap_block *)
                    ((uint8_t *)cur + BLOCK_HDR_SIZE + size);
                new_free->size = cur->size - size - BLOCK_HDR_SIZE;
                new_free->next = cur->next;
                new_free->magic = BLOCK_MAGIC_FREE;
                cur->size = size;
                cur->magic = BLOCK_MAGIC_ALLOC;
                if (prev)
                    prev->next = new_free;
                else
                    free_list = new_free;
            } else {
                cur->magic = BLOCK_MAGIC_ALLOC;
                if (prev)
                    prev->next = cur->next;
                else
                    free_list = cur->next;
            }
            cur->next = NULL;
            return (void *)((uint8_t *)cur + BLOCK_HDR_SIZE);
        }

        /* Need to split: put aligned block header at aligned - BLOCK_HDR_SIZE */
        if (waste >= BLOCK_HDR_SIZE + MIN_BLOCK_SIZE) {
            uint64_t total_needed = waste + size;
            if (cur->size >= total_needed) {
                /* Create the aligned block */
                struct kheap_block *aligned_block = (struct kheap_block *)(aligned - BLOCK_HDR_SIZE);
                aligned_block->size = size;
                aligned_block->next = NULL;
                aligned_block->magic = BLOCK_MAGIC_ALLOC;

                /* Shrink current free block to the waste area */
                uint64_t remaining = cur->size - waste - size;
                cur->size = waste - BLOCK_HDR_SIZE;

                /* If there's space after the aligned block, create another free block */
                if (remaining >= BLOCK_HDR_SIZE + MIN_BLOCK_SIZE) {
                    struct kheap_block *tail_free = (struct kheap_block *)
                        ((uint8_t *)aligned_block + BLOCK_HDR_SIZE + size);
                    tail_free->size = remaining - BLOCK_HDR_SIZE;
                    tail_free->next = cur->next;
                    tail_free->magic = BLOCK_MAGIC_FREE;
                    cur->next = tail_free;
                } else {
                    /* Give extra space to aligned block */
                    aligned_block->size += remaining;
                }

                return (void *)aligned;
            }
        }

        prev = cur;
        cur = cur->next;
    }

    return NULL;
}

void kfree(void *ptr) {
    if (!ptr)
        return;

    struct kheap_block *block = (struct kheap_block *)((uint8_t *)ptr - BLOCK_HDR_SIZE);

    /* Verify magic */
    if (block->magic != BLOCK_MAGIC_ALLOC) {
        debug_printf("KHEAP: kfree invalid block at 0x%x (magic=0x%x)\n",
                     (uint64_t)block, block->magic);
        return;
    }

    block->magic = BLOCK_MAGIC_FREE;

    /* Insert into free list in address order for coalescing */
    struct kheap_block *prev = NULL;
    struct kheap_block *cur = free_list;

    while (cur && cur < block) {
        prev = cur;
        cur = cur->next;
    }

    block->next = cur;
    if (prev)
        prev->next = block;
    else
        free_list = block;

    /* Coalesce with next block */
    if (block->next) {
        uint64_t block_end = (uint64_t)block + BLOCK_HDR_SIZE + block->size;
        if (block_end == (uint64_t)block->next) {
            block->size += BLOCK_HDR_SIZE + block->next->size;
            block->next = block->next->next;
        }
    }

    /* Coalesce with previous block */
    if (prev && prev->magic == BLOCK_MAGIC_FREE) {
        uint64_t prev_end = (uint64_t)prev + BLOCK_HDR_SIZE + prev->size;
        if (prev_end == (uint64_t)block) {
            prev->size += BLOCK_HDR_SIZE + block->size;
            prev->next = block->next;
        }
    }
}
