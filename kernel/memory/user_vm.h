#ifndef USER_VM_H
#define USER_VM_H

#include "../common.h"

/* User virtual address layout */
#define USER_CODE_BASE    0x400000ULL
#define USER_STACK_TOP    0x800000000ULL
#define USER_STACK_BOTTOM (USER_STACK_TOP - PAGE_SIZE)

/* Create a new user address space (PML4).
 * Copies kernel PML4 entries so kernel code/interrupts work.
 * Returns physical address of new PML4, or 0 on failure. */
uint64_t user_vm_create_address_space(void);

/* Map a page in a given address space (specified by PML4 physical address).
 * Propagates PTE_USER to all intermediate page table levels.
 * Returns 0 on success, -1 on failure. */
int user_vm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

#endif /* USER_VM_H */
