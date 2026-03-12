#include "rmap.h"
#include "pmm.h"
#include "../debug/debug.h"
#include "../string.h"

/* Max pages we track (512MB / 4KB = 131072 pages) */
#define RMAP_MAX_PAGES  131072

static struct rmap_entry rmap_table[RMAP_MAX_PAGES];

void rmap_init(void) {
    memset(rmap_table, 0, sizeof(rmap_table));
    debug_printf("rmap: initialized (%u entries)\n", (uint64_t)RMAP_MAX_PAGES);
}

void rmap_set(uint64_t phys, uint32_t pid, uint64_t virt) {
    uint64_t idx = phys / PAGE_SIZE;
    if (idx < RMAP_MAX_PAGES) {
        rmap_table[idx].pid = pid;
        rmap_table[idx].virt_addr = virt;
    }
}

void rmap_clear(uint64_t phys) {
    uint64_t idx = phys / PAGE_SIZE;
    if (idx < RMAP_MAX_PAGES) {
        rmap_table[idx].pid = 0;
        rmap_table[idx].virt_addr = 0;
    }
}

struct rmap_entry *rmap_get(uint64_t phys) {
    uint64_t idx = phys / PAGE_SIZE;
    if (idx < RMAP_MAX_PAGES && rmap_table[idx].pid != 0)
        return &rmap_table[idx];
    return NULL;
}
