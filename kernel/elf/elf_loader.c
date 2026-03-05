#include "elf_loader.h"
#include "elf.h"
#include "../memory/pmm.h"
#include "../memory/vmm.h"
#include "../memory/user_vm.h"
#include "../string.h"
#include "../debug/debug.h"

int elf_validate(const uint8_t *data, uint64_t size) {
    if (!data || size < sizeof(Elf64_Ehdr))
        return -1;

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;

    /* Check magic */
    if (ehdr->e_ident[EI_MAG0] != ELF_MAGIC0 ||
        ehdr->e_ident[EI_MAG1] != ELF_MAGIC1 ||
        ehdr->e_ident[EI_MAG2] != ELF_MAGIC2 ||
        ehdr->e_ident[EI_MAG3] != ELF_MAGIC3)
        return -1;

    /* Check class, endianness, version */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64)
        return -1;
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
        return -1;
    if (ehdr->e_ident[EI_VERSION] != EV_CURRENT)
        return -1;

    /* Check type and machine */
    if (ehdr->e_type != ET_EXEC)
        return -1;
    if (ehdr->e_machine != EM_X86_64)
        return -1;

    /* Check program headers are present and within bounds */
    if (ehdr->e_phnum == 0)
        return -1;
    if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * ehdr->e_phentsize > size)
        return -1;

    return 0;
}

int elf_load(uint64_t pml4_phys, const uint8_t *data, uint64_t size,
             struct elf_load_result *result) {
    if (elf_validate(data, size) < 0) {
        debug_printf("elf_load: validation failed\n");
        return -1;
    }

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;
    result->entry_point = ehdr->e_entry;

    /* Iterate program headers */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = (const Elf64_Phdr *)
            (data + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD)
            continue;

        /* Validate segment bounds in file */
        if (phdr->p_offset + phdr->p_filesz > size) {
            debug_printf("elf_load: segment %u extends beyond file\n",
                         (uint64_t)i);
            return -1;
        }

        /* Allocate and map pages for this segment */
        uint64_t seg_start = phdr->p_vaddr & ~(PAGE_SIZE - 1);
        uint64_t seg_end = (phdr->p_vaddr + phdr->p_memsz + PAGE_SIZE - 1) &
                           ~(PAGE_SIZE - 1);
        /* Always map writable — the trampoline needs to copy data in.
         * (A future optimization could remap read-only after loading.) */
        uint64_t flags = PTE_PRESENT | PTE_USER | PTE_WRITABLE;

        for (uint64_t addr = seg_start; addr < seg_end; addr += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                debug_printf("elf_load: out of physical pages\n");
                return -1;
            }
            memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
            if (user_vm_map_page(pml4_phys, addr, phys, flags) < 0) {
                debug_printf("elf_load: failed to map page at 0x%x\n", addr);
                return -1;
            }
        }

        debug_printf("elf_load: segment %u vaddr=0x%x filesz=%u memsz=%u\n",
                     (uint64_t)i, phdr->p_vaddr, phdr->p_filesz, phdr->p_memsz);
    }

    debug_printf("elf_load: entry=0x%x\n", result->entry_point);
    return 0;
}
