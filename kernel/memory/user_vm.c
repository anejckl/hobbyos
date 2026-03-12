#include "user_vm.h"
#include "pmm.h"
#include "vmm.h"
#include "rmap.h"
#include "../string.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
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

    /* Copy all kernel-half PML4 entries (256-511) from boot tables.
     * This covers: physical direct map (256), framebuffer (510),
     * kernel code/data/heap (511), and any future kernel mappings. */
    for (int i = 256; i < 512; i++)
        new_pml4[i] = boot_pml4[i];

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

    /* Set reverse mapping for user-space pages (needed by swap eviction) */
    if (virt < KERNEL_VMA) {
        struct process *cur = scheduler_get_current();
        if (cur)
            rmap_set(phys, cur->pid, virt);
    }

    return 0;
}

uint64_t *user_vm_get_pte(uint64_t pml4_phys, uint64_t virt) {
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);

    uint64_t pml4_idx = PML4_INDEX(virt);
    if (!(pml4[pml4_idx] & PTE_PRESENT))
        return NULL;
    uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(pml4[pml4_idx] & PTE_ADDR_MASK);

    uint64_t pdpt_idx = PDPT_INDEX(virt);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT))
        return NULL;
    if (pdpt[pdpt_idx] & PTE_HUGE)
        return NULL;
    uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    uint64_t pd_idx = PD_INDEX(virt);
    if (!(pd[pd_idx] & PTE_PRESENT))
        return NULL;
    if (pd[pd_idx] & PTE_HUGE)
        return NULL;
    uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(pd[pd_idx] & PTE_ADDR_MASK);

    uint64_t pt_idx = PT_INDEX(virt);
    return &pt[pt_idx];
}

uint64_t user_vm_fork_address_space(uint64_t parent_pml4_phys) {
    /* Create new PML4 with kernel entries */
    uint64_t child_pml4_phys = user_vm_create_address_space();
    if (!child_pml4_phys)
        return 0;

    uint64_t *parent_pml4 = (uint64_t *)PHYS_TO_VIRT(parent_pml4_phys);

    /* Walk user-space entries: PML4 indices 0-255 */
    for (int pml4_i = 0; pml4_i < 256; pml4_i++) {
        if (!(parent_pml4[pml4_i] & PTE_PRESENT))
            continue;

        uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(parent_pml4[pml4_i] & PTE_ADDR_MASK);

        for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            if (!(pdpt[pdpt_i] & PTE_PRESENT))
                continue;
            if (pdpt[pdpt_i] & PTE_HUGE)
                continue;

            uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(pdpt[pdpt_i] & PTE_ADDR_MASK);

            for (int pd_i = 0; pd_i < 512; pd_i++) {
                if (!(pd[pd_i] & PTE_PRESENT))
                    continue;
                if (pd[pd_i] & PTE_HUGE)
                    continue;

                uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(pd[pd_i] & PTE_ADDR_MASK);

                for (int pt_i = 0; pt_i < 512; pt_i++) {
                    if (!(pt[pt_i] & PTE_PRESENT))
                        continue;

                    uint64_t phys = pt[pt_i] & PTE_ADDR_MASK;
                    uint64_t flags = pt[pt_i] & ~PTE_ADDR_MASK;

                    /* Compute virtual address from indices */
                    uint64_t virt = ((uint64_t)pml4_i << 39) |
                                    ((uint64_t)pdpt_i << 30) |
                                    ((uint64_t)pd_i << 21) |
                                    ((uint64_t)pt_i << 12);

                    /* COW: if page was writable, mark as read-only + COW */
                    if (flags & PTE_WRITABLE) {
                        flags &= ~PTE_WRITABLE;
                        flags |= PTE_COW;
                        /* Update parent PTE to match */
                        pt[pt_i] = (phys & PTE_ADDR_MASK) | flags;
                    }

                    /* Share the same physical page */
                    pmm_page_ref(phys);

                    /* Map in child address space with same flags */
                    if (user_vm_map_page(child_pml4_phys, virt,
                                         phys, flags) < 0) {
                        debug_printf("user_vm: fork: map failed\n");
                        return 0;
                    }
                }
            }
        }
    }

    /* Flush parent's TLB since we changed its PTEs */
    write_cr3(read_cr3());

    debug_printf("user_vm: COW forked address space: parent=0x%x child=0x%x\n",
                 parent_pml4_phys, child_pml4_phys);
    return child_pml4_phys;
}

void user_vm_destroy_address_space(uint64_t pml4_phys) {
    uint64_t *pml4 = (uint64_t *)PHYS_TO_VIRT(pml4_phys);

    /* Walk user-space PML4 entries only (0-255), skip kernel entries (256, 511) */
    for (int pml4_i = 0; pml4_i < 256; pml4_i++) {
        if (!(pml4[pml4_i] & PTE_PRESENT))
            continue;

        uint64_t pdpt_phys = pml4[pml4_i] & PTE_ADDR_MASK;
        uint64_t *pdpt = (uint64_t *)PHYS_TO_VIRT(pdpt_phys);

        for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            if (!(pdpt[pdpt_i] & PTE_PRESENT))
                continue;
            if (pdpt[pdpt_i] & PTE_HUGE)
                continue;

            uint64_t pd_phys = pdpt[pdpt_i] & PTE_ADDR_MASK;
            uint64_t *pd = (uint64_t *)PHYS_TO_VIRT(pd_phys);

            for (int pd_i = 0; pd_i < 512; pd_i++) {
                if (!(pd[pd_i] & PTE_PRESENT))
                    continue;
                if (pd[pd_i] & PTE_HUGE)
                    continue;

                uint64_t pt_phys = pd[pd_i] & PTE_ADDR_MASK;
                uint64_t *pt = (uint64_t *)PHYS_TO_VIRT(pt_phys);

                /* Unref all leaf (data) pages in this PT */
                for (int pt_i = 0; pt_i < 512; pt_i++) {
                    if (!(pt[pt_i] & PTE_PRESENT))
                        continue;
                    uint64_t leaf_phys = pt[pt_i] & PTE_ADDR_MASK;
                    rmap_clear(leaf_phys);
                    pmm_page_unref(leaf_phys);
                }

                /* Free the PT page itself */
                pmm_free_page(pt_phys);
            }

            /* Free the PD page */
            pmm_free_page(pd_phys);
        }

        /* Free the PDPT page */
        pmm_free_page(pdpt_phys);
    }

    /* Free the PML4 page itself */
    pmm_free_page(pml4_phys);

    debug_printf("user_vm: destroyed address space PML4=0x%x\n", pml4_phys);
}

int cow_handle_fault(uint64_t pml4_phys, uint64_t fault_addr) {
    uint64_t *pte = user_vm_get_pte(pml4_phys, fault_addr);
    if (!pte)
        return -1;

    /* Check if this is a COW page */
    if (!(*pte & PTE_PRESENT) || !(*pte & PTE_COW))
        return -1;

    uint64_t old_phys = *pte & PTE_ADDR_MASK;
    uint64_t flags = *pte & ~PTE_ADDR_MASK;

    if (pmm_page_refcount(old_phys) == 1) {
        /* We're the sole owner — just make it writable again */
        *pte = (old_phys & PTE_ADDR_MASK) | (flags & ~PTE_COW) | PTE_WRITABLE;
        invlpg(fault_addr & ~(uint64_t)(PAGE_SIZE - 1));
        return 0;
    }

    /* Multiple owners — allocate a new page and copy */
    uint64_t new_phys = pmm_alloc_page();
    if (!new_phys)
        return -1;

    memcpy(PHYS_TO_VIRT(new_phys), PHYS_TO_VIRT(old_phys), PAGE_SIZE);
    pmm_page_unref(old_phys);

    /* Update PTE: new page, writable, no COW */
    *pte = (new_phys & PTE_ADDR_MASK) | (flags & ~PTE_COW) | PTE_WRITABLE;
    invlpg(fault_addr & ~(uint64_t)(PAGE_SIZE - 1));

    return 0;
}
