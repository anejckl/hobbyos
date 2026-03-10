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

        /* Determine page permissions from ELF segment flags */
        uint64_t flags = PTE_PRESENT | PTE_USER;
        if (phdr->p_flags & PF_W)   flags |= PTE_WRITABLE;
        if (!(phdr->p_flags & PF_X)) flags |= PTE_NX;

        uint64_t seg_start = phdr->p_vaddr & ~(PAGE_SIZE - 1);
        uint64_t seg_end = (phdr->p_vaddr + phdr->p_memsz + PAGE_SIZE - 1) &
                           ~(PAGE_SIZE - 1);

        for (uint64_t addr = seg_start; addr < seg_end; addr += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                debug_printf("elf_load: out of physical pages\n");
                return -1;
            }
            uint8_t *kpage = (uint8_t *)PHYS_TO_VIRT(phys);
            memset(kpage, 0, PAGE_SIZE);

            /* Copy file bytes that overlap this page */
            uint64_t seg_file_end = phdr->p_vaddr + phdr->p_filesz;
            uint64_t copy_start = phdr->p_vaddr > addr ? phdr->p_vaddr : addr;
            uint64_t copy_end   = seg_file_end  < addr + PAGE_SIZE ? seg_file_end : addr + PAGE_SIZE;
            if (copy_start < copy_end) {
                uint64_t dst_off = copy_start - addr;
                uint64_t src_off = phdr->p_offset + (copy_start - phdr->p_vaddr);
                memcpy(kpage + dst_off, data + src_off, copy_end - copy_start);
            }

            if (user_vm_map_page(pml4_phys, addr, phys, flags) < 0) {
                debug_printf("elf_load: failed to map page at 0x%x\n", addr);
                return -1;
            }
        }

        debug_printf("elf_load: segment %u vaddr=0x%x filesz=%u memsz=%u flags=0x%x\n",
                     (uint64_t)i, phdr->p_vaddr, phdr->p_filesz,
                     phdr->p_memsz, (uint64_t)phdr->p_flags);
    }

    debug_printf("elf_load: entry=0x%x\n", result->entry_point);
    return 0;
}

/* Set up SysV AMD64 ABI stack layout in a physical page.
 * stack_phys     - physical address of the 4 KB stack page
 * stack_base_virt - user virtual address at which the page is mapped
 * argc / argv    - program arguments (argv[0] = program name)
 * rsp_out        - set to user RSP (points to argc value on stack)
 * argv_ptr_out   - set to user virtual address of argv[0] pointer
 * Returns 0 on success, -1 if arguments don't fit in one page. */
int elf_setup_stack(uint64_t stack_phys, uint64_t stack_base_virt,
                    int argc, const char **argv,
                    uint64_t *rsp_out, uint64_t *argv_ptr_out) {
#define ELF_MAX_ARGS    32
#define ELF_MAX_ARG_LEN 256

    if (argc > ELF_MAX_ARGS)
        argc = ELF_MAX_ARGS;

    uint8_t *kpage = (uint8_t *)PHYS_TO_VIRT(stack_phys);
    uint64_t argv_ptrs[ELF_MAX_ARGS];
    int write_pos = PAGE_SIZE;  /* index into kpage; strings written downward */

    /* Write argv strings from top of page downward */
    for (int i = argc - 1; i >= 0; i--) {
        const char *s = argv[i] ? argv[i] : "";
        int len = 0;
        while (s[len] && len < ELF_MAX_ARG_LEN - 1) len++;

        if (write_pos < len + 1)
            return -1;

        write_pos -= len + 1;
        memcpy(kpage + write_pos, s, (uint64_t)len);
        kpage[write_pos + len] = '\0';
        argv_ptrs[i] = stack_base_virt + (uint64_t)write_pos;
    }

    /* Block: [argc][argv[0]..argv[argc-1]][NULL][NULL(envp)] */
    int block_size = (argc + 3) * 8;
    if (write_pos < block_size)
        return -1;

    /* Find 16-byte aligned start for the block */
    int start = (write_pos - block_size) & ~15;
    if (start < 0)
        return -1;

    /* Write block into physical page via kernel virtual mapping */
    uint64_t *p = (uint64_t *)(kpage + start);
    *p++ = (uint64_t)argc;
    for (int i = 0; i < argc; i++)
        *p++ = argv_ptrs[i];
    *p++ = 0;  /* argv[argc] = NULL */
    *p   = 0;  /* envp[0]   = NULL */

    *rsp_out      = stack_base_virt + (uint64_t)start;
    *argv_ptr_out = stack_base_virt + (uint64_t)start + 8;
    return 0;
}
