#ifndef USER_VM_H
#define USER_VM_H

#include "../common.h"

/* User virtual address layout */
#define USER_CODE_BASE    0x400000ULL
#define USER_STACK_TOP    0x800000000ULL
#define USER_STACK_PAGES  4  /* 16 KB user stack */
#define USER_STACK_BOTTOM (USER_STACK_TOP - (USER_STACK_PAGES * PAGE_SIZE))

/* Create a new user address space (PML4).
 * Copies kernel PML4 entries so kernel code/interrupts work.
 * Returns physical address of new PML4, or 0 on failure. */
uint64_t user_vm_create_address_space(void);

/* Map a page in a given address space (specified by PML4 physical address).
 * Propagates PTE_USER to all intermediate page table levels.
 * Returns 0 on success, -1 on failure. */
int user_vm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

/* Clone a parent address space for fork (copies user-space pages).
 * Returns physical address of new PML4, or 0 on failure. */
uint64_t user_vm_fork_address_space(uint64_t parent_pml4_phys);

/* Get pointer to leaf PTE for a virtual address.
 * Returns NULL if any level is not present. */
uint64_t *user_vm_get_pte(uint64_t pml4_phys, uint64_t virt);

/* Handle a COW page fault. Returns 0 if handled, -1 if not a COW fault. */
int cow_handle_fault(uint64_t pml4_phys, uint64_t fault_addr);

/* Destroy a user address space: free all user pages and page tables.
 * Must be called while NOT using this PML4 as the active CR3.
 * Handles COW pages correctly (decrements refcount, frees only if sole owner). */
void user_vm_destroy_address_space(uint64_t pml4_phys);

#endif /* USER_VM_H */
