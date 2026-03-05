#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "../common.h"

/* Result of loading an ELF binary */
struct elf_load_result {
    uint64_t entry_point;   /* Virtual address of entry point */
};

/* Validate an ELF64 header.
 * data - pointer to ELF file data
 * size - size of ELF file in bytes
 * Returns 0 if valid, -1 if invalid. */
int elf_validate(const uint8_t *data, uint64_t size);

/* Load an ELF64 executable into a user address space.
 * pml4_phys - physical address of the target PML4
 * data      - pointer to ELF file data
 * size      - size of ELF file in bytes
 * result    - output: entry point and load info
 * Returns 0 on success, -1 on failure. */
int elf_load(uint64_t pml4_phys, const uint8_t *data, uint64_t size,
             struct elf_load_result *result);

#endif /* ELF_LOADER_H */
