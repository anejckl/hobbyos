#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "../common.h"

/* ld.so load base — above the mmap region */
#define LDSO_BASE  0x7FC0000000ULL

/* Result of loading an ELF binary */
struct elf_load_result {
    uint64_t entry_point;   /* ld.so entry if PT_INTERP present, else main binary entry */
    uint64_t load_end;      /* first page after all PT_LOAD segments (for brk_start) */
    uint64_t at_phdr;       /* user vaddr of program header table */
    uint16_t at_phent;
    uint16_t at_phnum;
    uint64_t at_base;       /* ld.so load base (LDSO_BASE) */
    uint64_t at_entry;      /* main binary entry (before ld.so intercepts) */
};

/* Validate an ELF64 header.
 * data - pointer to ELF file data
 * size - size of ELF file in bytes
 * Returns 0 if valid, -1 if invalid. */
int elf_validate(const uint8_t *data, uint64_t size);

/* Load an ELF64 executable into a user address space.
 * pml4_phys  - physical address of the target PML4
 * data       - pointer to ELF file data
 * size       - size of ELF file in bytes
 * base_addr  - load base address (0 for ET_EXEC, LDSO_BASE for ld.so)
 * result     - output: entry point and load info
 * Returns 0 on success, -1 on failure. */
int elf_load(uint64_t pml4_phys, const uint8_t *data, uint64_t size,
             uint64_t base_addr, struct elf_load_result *result);

/* Set up SysV AMD64 ABI argc/argv stack layout in a physical stack page.
 * stack_phys      - physical address of the 4 KB stack page
 * stack_base_virt - user virtual address at which the page is mapped
 * argc / argv     - program arguments (argv[0] = program name)
 * result          - if non-NULL, auxv entries are appended after envp NULL
 * rsp_out         - set to user RSP (points to argc on stack, 16-byte aligned)
 * argv_ptr_out    - set to user virtual address of argv[0] pointer
 * Returns 0 on success, -1 if arguments don't fit. */
int elf_setup_stack(uint64_t stack_phys, uint64_t stack_base_virt,
                    int argc, const char **argv,
                    const struct elf_load_result *result,
                    uint64_t *rsp_out, uint64_t *argv_ptr_out);

#endif /* ELF_LOADER_H */
