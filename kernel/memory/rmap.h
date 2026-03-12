#ifndef RMAP_H
#define RMAP_H

#include "../common.h"

/* Reverse map: physical page → (pid, virtual address)
 * Used by swap eviction to find which PTE to update. */

struct rmap_entry {
    uint32_t pid;
    uint64_t virt_addr;
};

/* Initialize rmap (call after PMM init) */
void rmap_init(void);

/* Set reverse mapping for a physical page */
void rmap_set(uint64_t phys, uint32_t pid, uint64_t virt);

/* Clear reverse mapping */
void rmap_clear(uint64_t phys);

/* Get reverse mapping for a physical page */
struct rmap_entry *rmap_get(uint64_t phys);

#endif /* RMAP_H */
