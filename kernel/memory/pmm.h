#ifndef PMM_H
#define PMM_H

#include "../common.h"

void pmm_init(uint32_t multiboot_info_phys);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t phys_addr);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_total_pages(void);

#endif /* PMM_H */
