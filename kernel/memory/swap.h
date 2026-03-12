#ifndef SWAP_H
#define SWAP_H

#include "../common.h"

#define SWAP_MAX_PAGES  4096          /* 16 MB swap space */
#define SWAP_START_LBA  32768         /* after 16MB ext2 region */
#define PTE_SWAPPED     (1ULL << 10)  /* OS-available bit: page is in swap */

/* Initialize swap subsystem */
void swap_init(void);

/* Evict one page to swap using CLOCK algorithm.
 * Returns 0 on success, -1 if nothing could be evicted. */
int swap_evict_one(void);

/* Handle a swap-in on page fault. Checks if PTE has PTE_SWAPPED set.
 * Returns 0 if handled (page swapped in), -1 if not a swap fault. */
int swap_handle_fault(uint64_t pml4_phys, uint64_t fault_addr);

/* Get swap statistics */
uint64_t swap_get_used(void);
uint64_t swap_get_total(void);

#endif /* SWAP_H */
