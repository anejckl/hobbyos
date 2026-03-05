#include "user_vm.h"
#include "pmm.h"
#include "vmm.h"
#include "../string.h"
#include "../debug/debug.h"

/* Page table index extraction macros */
#define PML4_INDEX(addr)  (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)  (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)    (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)    (((addr) >> 12) & 0x1FF)

#define PTE_ADDR_MASK     0x000FFFFFFFFFF000ULL

/* Allocate a zeroed page, return physical address */
static uint64_t alloc_zeroed_page(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0)
        return 0;
    memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
    return phys;
}

uint64_t user_vm_create_address_space(void) {
    /* Allocate new PML4 */
    uint64_t pml4_phys = alloc_zeroed_page();
    if (!pml4_phys)
        return 0;

    uint64_t *new_pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);

    /* Copy kernel entries from boot PML4 */
    uint64_t boot_cr3 = read_cr3();
    uint64_t *boot_pml4 = (uint64_t *)PHYS_TO_VIRT(boot_cr3 & PTE_ADDR_MASK);

    /* PML4[256] = physical memory direct map (PHYS_MAP_BASE) */
    new_pml4[256] = boot_pml4[256];
    /* PML4[511] = kernel code/data (KERNEL_VMA) */
    new_pml4[511] = boot_pml4[511];

    debug_printf("user_vm: created address space PML4=0x%x\n", pml4_phys);
    return pml4_phys;
}

int user_vm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);

    /* PML4 -> PDPT */
    uint64_t pml4_idx = PML4_INDEX(virt);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) {
        uint64_t table = alloc_zeroed_page();
        if (!table) return -1;
        pml4[pml4_idx] = table | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    } else {
        pml4[pml4_idx] |= PTE_USER | PTE_WRITABLE;
    }
    uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(pml4[pml4_idx] & PTE_ADDR_MASK);

    /* PDPT -> PD */
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        uint64_t table = alloc_zeroed_page();
        if (!table) return -1;
        pdpt[pdpt_idx] = table | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    } else {
        pdpt[pdpt_idx] |= PTE_USER | PTE_WRITABLE;
    }
    uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    /* PD -> PT */
    uint64_t pd_idx = PD_INDEX(virt);
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        uint64_t table = alloc_zeroed_page();
        if (!table) return -1;
        pd[pd_idx] = table | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    } else {
        pd[pd_idx] |= PTE_USER | PTE_WRITABLE;
    }
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(pd[pd_idx] & PTE_ADDR_MASK);

    /* Set final PTE */
    uint64_t pt_idx = PT_INDEX(virt);
    pt[pt_idx] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;

    return 0;
}
