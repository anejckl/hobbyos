#include "swap.h"
#include "pmm.h"
#include "vmm.h"
#include "user_vm.h"
#include "rmap.h"
#include "../drivers/ata.h"
#include "../process/process.h"
#include "../string.h"
#include "../debug/debug.h"

/* Swap slot table: tracks which slots are in use */
static bool swap_slots[SWAP_MAX_PAGES];
static uint64_t swap_used = 0;
static bool swap_ready = false;

/* CLOCK hand for eviction */
static uint64_t clock_hand = 0;

void swap_init(void) {
    memset(swap_slots, 0, sizeof(swap_slots));
    swap_used = 0;
    swap_ready = true;
    debug_printf("swap: initialized %u pages (%u MB) at LBA %u\n",
                 (uint64_t)SWAP_MAX_PAGES,
                 (uint64_t)(SWAP_MAX_PAGES * PAGE_SIZE / (1024 * 1024)),
                 (uint64_t)SWAP_START_LBA);
}

/* Allocate a swap slot. Returns slot index, or -1 if full. */
static int swap_alloc_slot(void) {
    for (uint64_t i = 0; i < SWAP_MAX_PAGES; i++) {
        if (!swap_slots[i]) {
            swap_slots[i] = true;
            swap_used++;
            return (int)i;
        }
    }
    return -1;
}

/* Free a swap slot */
static void swap_free_slot(int slot) {
    if (slot >= 0 && slot < SWAP_MAX_PAGES && swap_slots[slot]) {
        swap_slots[slot] = false;
        swap_used--;
    }
}

/* Write a 4KB page to swap slot */
static int swap_write_page(int slot, uint64_t phys) {
    uint32_t lba = SWAP_START_LBA + (uint32_t)slot * 8;  /* 8 sectors per 4KB */
    return ata_write_sectors(lba, 8, (uint8_t *)PHYS_TO_VIRT(phys));
}

/* Read a 4KB page from swap slot */
static int swap_read_page(int slot, uint64_t phys) {
    uint32_t lba = SWAP_START_LBA + (uint32_t)slot * 8;
    return ata_read_sectors(lba, 8, (uint8_t *)PHYS_TO_VIRT(phys));
}

int swap_evict_one(void) {
    if (!swap_ready)
        return -1;

    /* CLOCK algorithm: scan user physical pages */
    uint64_t total = pmm_get_total_pages();
    uint64_t start = clock_hand;

    for (uint64_t j = 0; j < total * 2; j++) {
        uint64_t page_idx = (start + j) % total;
        uint64_t phys = page_idx * PAGE_SIZE;

        /* Only evict pages with refcount == 1 (single owner) */
        if (pmm_page_refcount(phys) != 1)
            continue;

        /* Look up reverse map */
        struct rmap_entry *rm = rmap_get(phys);
        if (!rm || rm->pid == 0)
            continue;

        struct process *proc = process_get_by_pid(rm->pid);
        if (!proc || !proc->cr3)
            continue;

        uint64_t *pte = user_vm_get_pte(proc->cr3, rm->virt_addr);
        if (!pte || !(*pte & PTE_PRESENT))
            continue;

        /* Check PTE_ACCESSED (bit 5) */
        if (*pte & PTE_ACCESSED) {
            /* Clear accessed bit, skip this page */
            *pte &= ~PTE_ACCESSED;
            continue;
        }

        /* This page hasn't been accessed recently — evict it */
        int slot = swap_alloc_slot();
        if (slot < 0)
            return -1;

        /* Write page to swap */
        if (swap_write_page(slot, phys) < 0) {
            swap_free_slot(slot);
            return -1;
        }

        /* Update PTE: not-present, store swap slot in upper bits */
        *pte = ((uint64_t)slot << 12) | PTE_SWAPPED;

        /* Invalidate TLB entry */
        invlpg(rm->virt_addr);

        /* Free physical page */
        rmap_clear(phys);
        pmm_page_unref(phys);

        clock_hand = (page_idx + 1) % total;

        debug_printf("swap: evicted page phys=0x%x virt=0x%x pid=%u -> slot %d\n",
                     phys, rm->virt_addr, (uint64_t)rm->pid, (int64_t)slot);
        return 0;
    }

    return -1;  /* Nothing evictable */
}

int swap_handle_fault(uint64_t pml4_phys, uint64_t fault_addr) {
    if (!swap_ready)
        return -1;

    uint64_t page_addr = fault_addr & ~(PAGE_SIZE - 1);
    uint64_t *pte = user_vm_get_pte(pml4_phys, page_addr);
    if (!pte)
        return -1;

    /* Check if this is a swapped page */
    if (!(*pte & PTE_SWAPPED) || (*pte & PTE_PRESENT))
        return -1;

    int slot = (int)(*pte >> 12);

    /* Allocate fresh physical page */
    uint64_t new_phys = pmm_alloc_page();
    if (!new_phys) {
        /* Try to free a page first */
        if (swap_evict_one() == 0)
            new_phys = pmm_alloc_page();
        if (!new_phys)
            return -1;
    }

    /* Read from swap */
    if (swap_read_page(slot, new_phys) < 0) {
        pmm_page_unref(new_phys);
        return -1;
    }

    /* Restore PTE */
    *pte = new_phys | PTE_PRESENT | PTE_USER | PTE_WRITABLE;
    invlpg(page_addr);

    /* Free swap slot */
    swap_free_slot(slot);

    /* Update rmap */
    struct process *cur = NULL;
    struct process *table = process_table_get();
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].cr3 == pml4_phys && table[i].state != PROCESS_UNUSED) {
            cur = &table[i];
            break;
        }
    }
    if (cur)
        rmap_set(new_phys, cur->pid, page_addr);

    debug_printf("swap: restored page virt=0x%x from slot %d\n",
                 page_addr, (int64_t)slot);
    return 0;
}

uint64_t swap_get_used(void) {
    return swap_used;
}

uint64_t swap_get_total(void) {
    return SWAP_MAX_PAGES;
}
