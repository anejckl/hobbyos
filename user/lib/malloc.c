#include "libc.h"

/* Heap block header */
struct malloc_block {
    size_t size;            /* payload size (excluding header) */
    int    free;            /* 1 = free, 0 = allocated */
    struct malloc_block *next;
};

#define HEADER_SIZE  (sizeof(struct malloc_block))
#define MIN_SPLIT    (HEADER_SIZE + 16)

static struct malloc_block *heap_head = NULL;
static uint64_t heap_end = 0;

/* Extend heap by 'sz' bytes via brk syscall */
static void *sbrk(size_t sz) {
    if (heap_end == 0) {
        heap_end = sys_brk_libc(0);
        if (heap_end == (uint64_t)-1) return NULL;
    }
    uint64_t old_end = heap_end;
    uint64_t new_end = heap_end + sz;
    uint64_t result = sys_brk_libc(new_end);
    if (result == (uint64_t)-1 || result < new_end) return NULL;
    heap_end = result;
    return (void *)old_end;
}

void *malloc(size_t size) {
    if (!size) return NULL;
    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    /* First fit */
    struct malloc_block *b = heap_head;
    while (b) {
        if (b->free && b->size >= size) {
            /* Split if enough room */
            if (b->size >= size + MIN_SPLIT) {
                struct malloc_block *rest =
                    (struct malloc_block *)((char *)b + HEADER_SIZE + size);
                rest->size = b->size - size - HEADER_SIZE;
                rest->free = 1;
                rest->next = b->next;
                b->next = rest;
                b->size = size;
            }
            b->free = 0;
            return (char *)b + HEADER_SIZE;
        }
        b = b->next;
    }

    /* No free block — extend heap */
    struct malloc_block *nb = (struct malloc_block *)sbrk(HEADER_SIZE + size);
    if (!nb) return NULL;
    nb->size = size;
    nb->free = 0;
    nb->next = NULL;

    if (!heap_head) {
        heap_head = nb;
    } else {
        /* Append to list */
        b = heap_head;
        while (b->next) b = b->next;
        b->next = nb;
    }
    return (char *)nb + HEADER_SIZE;
}

void free(void *ptr) {
    if (!ptr) return;
    struct malloc_block *b =
        (struct malloc_block *)((char *)ptr - HEADER_SIZE);
    b->free = 1;

    /* Coalesce with next */
    while (b->next && b->next->free) {
        b->size += HEADER_SIZE + b->next->size;
        b->next = b->next->next;
    }
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (!size) { free(ptr); return NULL; }

    struct malloc_block *b =
        (struct malloc_block *)((char *)ptr - HEADER_SIZE);
    if (b->size >= size) return ptr;

    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, b->size);
    free(ptr);
    return new_ptr;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}
