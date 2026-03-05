#ifndef PMM_H
#define PMM_H

#include "../common.h"

void pmm_init(uint32_t multiboot_info_phys);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t phys_addr);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_total_pages(void);

/* Page reference counting (for COW) */
void pmm_page_ref(uint64_t phys_addr);
void pmm_page_unref(uint64_t phys_addr);
uint8_t pmm_page_refcount(uint64_t phys_addr);

#endif /* PMM_H */
