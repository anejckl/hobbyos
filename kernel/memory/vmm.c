#include "vmm.h"
#include "pmm.h"
#include "../string.h"
#include "../debug/debug.h"

/* Page table index extraction macros */
#define PML4_INDEX(addr)  (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)  (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)    (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)    (((addr) >> 12) & 0x1FF)

#define PTE_ADDR_MASK     0x000FFFFFFFFFF000ULL

/* Get current PML4 from CR3 */
static uint64_t *get_pml4(void) {
    uint64_t cr3 = read_cr3();
    return (uint64_t *)PHYS_TO_VIRT(cr3 & PTE_ADDR_MASK);
}

/* Allocate a page table page, zero it, return its physical address */
static uint64_t alloc_table(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0)
        return 0;
    void *virt = PHYS_TO_VIRT(phys);
    memset(virt, 0, PAGE_SIZE);
    return phys;
}

/* Walk/create page table entries down to PT level */
static uint64_t *vmm_walk(uint64_t virt, bool create) {
    uint64_t *pml4 = get_pml4();

    /* PML4 → PDPT */
    uint64_t pml4_idx = PML4_INDEX(virt);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) {
        if (!create) return NULL;
        uint64_t phys = alloc_table();
        if (!phys) return NULL;
        pml4[pml4_idx] = phys | PTE_PRESENT | PTE_WRITABLE;
    }
    uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(pml4[pml4_idx] & PTE_ADDR_MASK);

    /* PDPT → PD */
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        if (!create) return NULL;
        uint64_t phys = alloc_table();
        if (!phys) return NULL;
        pdpt[pdpt_idx] = phys | PTE_PRESENT | PTE_WRITABLE;
    }
    uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    /* PD → PT */
    uint64_t pd_idx = PD_INDEX(virt);
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        if (!create) return NULL;
        uint64_t phys = alloc_table();
        if (!phys) return NULL;
        pd[pd_idx] = phys | PTE_PRESENT | PTE_WRITABLE;
    }
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(pd[pd_idx] & PTE_ADDR_MASK);

    return &pt[PT_INDEX(virt)];
}

void vmm_init(void) {
    debug_printf("VMM: Using existing boot page tables, CR3=0x%x\n", read_cr3());
}

int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pte = vmm_walk(virt, true);
    if (!pte)
        return -1;
    *pte = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    invlpg(virt);
    return 0;
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t *pte = vmm_walk(virt, false);
    if (pte) {
        *pte = 0;
        invlpg(virt);
    }
}

uint64_t vmm_get_physical(uint64_t virt) {
    uint64_t *pte = vmm_walk(virt, false);
    if (!pte || !(*pte & PTE_PRESENT))
        return 0;
    return (*pte & PTE_ADDR_MASK) | (virt & 0xFFF);
}

void vmm_map_range(uint64_t phys_base, uint64_t virt_base, uint64_t n_pages, uint64_t flags) {
    for (uint64_t i = 0; i < n_pages; i++) {
        vmm_map_page(virt_base + i * PAGE_SIZE,
                     phys_base + i * PAGE_SIZE,
                     flags);
    }
}
