#include "elf_loader.h"
#include "elf.h"
#include "../memory/pmm.h"
#include "../memory/vmm.h"
#include "../memory/user_vm.h"
#include "../memory/mmap.h"
#include "../memory/kheap.h"
#include "../fs/ext2.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
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

    /* Accept ET_EXEC or ET_DYN */
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN)
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

/* Load PT_LOAD segments from an ELF using demand paging (VMA_ELF entries).
 * Falls back to eager loading if the current process has no VMA support (kernel context).
 * base_addr is added to all virtual addresses (0 for ET_EXEC, LDSO_BASE for ld.so).
 * elf_data_owned indicates whether the elf data buffer should be freed on VMA destroy.
 * Updates *load_end_out with the highest loaded page end. */
static int elf_load_segments(uint64_t pml4_phys, const uint8_t *data, uint64_t size,
                              uint64_t base_addr, uint64_t *load_end_out,
                              bool demand_paging, bool elf_data_owned,
                              struct process *target_proc) {
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;
    uint64_t load_end = 0;

    struct process *cur = target_proc ? target_proc : scheduler_get_current();

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
        uint64_t pte_flags = PTE_PRESENT | PTE_USER;
        if (phdr->p_flags & PF_W)   pte_flags |= PTE_WRITABLE;
        if (!(phdr->p_flags & PF_X)) pte_flags |= PTE_NX;

        uint32_t prot = 0;
        if (phdr->p_flags & PF_R) prot |= PROT_READ;
        if (phdr->p_flags & PF_W) prot |= PROT_WRITE;
        if (phdr->p_flags & PF_X) prot |= PROT_EXEC;

        uint64_t vaddr = phdr->p_vaddr + base_addr;
        uint64_t seg_start = vaddr & ~(PAGE_SIZE - 1);
        uint64_t seg_end = (vaddr + phdr->p_memsz + PAGE_SIZE - 1) &
                           ~(PAGE_SIZE - 1);

        if (seg_end > load_end)
            load_end = seg_end;

        /* Try demand paging via VMA_ELF */
        if (demand_paging && cur) {
            vma_t *vma = vma_alloc(cur);
            if (vma) {
                memset(vma, 0, sizeof(*vma));
                vma->start = seg_start;
                vma->end = seg_end;
                vma->prot = prot;
                vma->flags = MAP_PRIVATE;
                vma->type = VMA_ELF;
                vma->in_use = true;
                vma->elf_data = data + phdr->p_offset;
                vma->elf_data_filesz = phdr->p_filesz;
                vma->elf_vaddr = seg_start;
                /* Only the first segment "owns" the data for freeing purposes */
                vma->elf_data_owned = false;

                debug_printf("elf_load: demand-page segment %u vaddr=0x%x-0x%x filesz=%u\n",
                             (uint64_t)i, seg_start, seg_end, phdr->p_filesz);
                continue;
            }
            /* Fall through to eager loading if no VMA slot */
        }

        /* Eager loading fallback */
        for (uint64_t addr = seg_start; addr < seg_end; addr += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                debug_printf("elf_load: out of physical pages\n");
                return -1;
            }
            uint8_t *kpage = (uint8_t *)PHYS_TO_VIRT(phys);
            memset(kpage, 0, PAGE_SIZE);

            /* Copy file bytes that overlap this page */
            uint64_t seg_file_end = vaddr + phdr->p_filesz;
            uint64_t copy_start = vaddr > addr ? vaddr : addr;
            uint64_t copy_end   = seg_file_end < addr + PAGE_SIZE ? seg_file_end : addr + PAGE_SIZE;
            if (copy_start < copy_end) {
                uint64_t dst_off = copy_start - addr;
                uint64_t src_off = phdr->p_offset + (copy_start - vaddr);
                memcpy(kpage + dst_off, data + src_off, copy_end - copy_start);
            }

            if (user_vm_map_page(pml4_phys, addr, phys, pte_flags) < 0) {
                debug_printf("elf_load: failed to map page at 0x%x\n", addr);
                return -1;
            }
        }

        debug_printf("elf_load: eager segment %u vaddr=0x%x filesz=%u memsz=%u flags=0x%x\n",
                     (uint64_t)i, vaddr, phdr->p_filesz,
                     phdr->p_memsz, (uint64_t)phdr->p_flags);
    }

    *load_end_out = load_end;
    return 0;
}

int elf_load(uint64_t pml4_phys, const uint8_t *data, uint64_t size,
             uint64_t base_addr, struct elf_load_result *result,
             struct process *target_proc) {
    if (elf_validate(data, size) < 0) {
        debug_printf("elf_load: validation failed\n");
        return -1;
    }

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;

    /* Fill in main binary info */
    result->at_entry    = ehdr->e_entry + base_addr;
    result->at_phdr     = ehdr->e_phoff + base_addr;
    result->at_phent    = ehdr->e_phentsize;
    result->at_phnum    = ehdr->e_phnum;
    result->at_base     = 0;
    result->load_end    = 0;

    /* Default entry is main binary entry */
    result->entry_point = ehdr->e_entry + base_addr;

    /* Load PT_LOAD segments with demand paging */
    uint64_t load_end = 0;
    if (elf_load_segments(pml4_phys, data, size, base_addr, &load_end, true, false, target_proc) < 0)
        return -1;
    result->load_end = load_end;

    /* Scan for PT_INTERP — if present, load ld.so */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = (const Elf64_Phdr *)
            (data + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type == PT_INTERP) {
            /* Read interpreter path from the file */
            if (phdr->p_offset + phdr->p_filesz > size)
                break;
            /* Typically "/lib/ld.so" */
            const char *interp = (const char *)(data + phdr->p_offset);
            debug_printf("elf_load: PT_INTERP = %s\n", interp);

            /* Try to load ld.so from ext2 */
            if (!ext2_is_mounted())
                break;

            uint32_t ino = ext2_path_lookup(interp);
            if (!ino) {
                debug_printf("elf_load: ld.so not found at %s\n", interp);
                break;
            }

            struct ext2_inode ldso_inode;
            if (ext2_read_inode(ino, &ldso_inode) != 0 || ldso_inode.i_size == 0)
                break;

            uint8_t *ldso_buf = (uint8_t *)kmalloc(ldso_inode.i_size);
            if (!ldso_buf)
                break;

            int bytes = ext2_read_file(&ldso_inode, 0, ldso_inode.i_size, ldso_buf);
            if (bytes <= 0)
                break;

            /* Validate ld.so ELF */
            if (elf_validate(ldso_buf, (uint64_t)bytes) < 0) {
                debug_printf("elf_load: ld.so invalid ELF\n");
                kfree(ldso_buf);
                break;
            }

            const Elf64_Ehdr *ldso_ehdr = (const Elf64_Ehdr *)ldso_buf;

            /* Load ld.so at LDSO_BASE (eager — it's small) */
            uint64_t ldso_load_end = 0;
            if (elf_load_segments(pml4_phys, ldso_buf, (uint64_t)bytes,
                                  LDSO_BASE, &ldso_load_end, false, false, target_proc) == 0) {
                result->at_base     = LDSO_BASE;
                result->entry_point = ldso_ehdr->e_entry + LDSO_BASE;
                debug_printf("elf_load: ld.so loaded at 0x%x entry=0x%x\n",
                             (uint64_t)LDSO_BASE, result->entry_point);
            }
            kfree(ldso_buf);
            break;
        }
    }

    debug_printf("elf_load: entry=0x%x load_end=0x%x\n",
                 result->entry_point, result->load_end);
    return 0;
}

/* Set up SysV AMD64 ABI stack layout in a physical page.
 * stack_phys     - physical address of the 4 KB stack page
 * stack_base_virt - user virtual address at which the page is mapped
 * argc / argv    - program arguments (argv[0] = program name)
 * result         - if non-NULL, auxv entries appended after envp NULL
 * rsp_out        - set to user RSP (points to argc value on stack)
 * argv_ptr_out   - set to user virtual address of argv[0] pointer
 * Returns 0 on success, -1 if arguments don't fit in one page. */
int elf_setup_stack(uint64_t stack_phys, uint64_t stack_base_virt,
                    int argc, const char **argv,
                    const struct elf_load_result *result,
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

    /* Determine auxv count */
    int auxv_count = 0;
    if (result) {
        /* AT_PHDR, AT_PHENT, AT_PHNUM, AT_PAGESZ, AT_BASE, AT_FLAGS, AT_ENTRY, AT_NULL */
        auxv_count = 8;
    }

    /* Block: [argc][argv[0]..argv[argc-1]][NULL][NULL(envp)][auxv pairs...][AT_NULL] */
    int block_size = (argc + 3 + auxv_count * 2) * 8;
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
    *p++ = 0;  /* envp[0]   = NULL */

    /* Append auxv if result provided */
    if (result) {
        *p++ = AT_PHDR;  *p++ = result->at_phdr;
        *p++ = AT_PHENT; *p++ = (uint64_t)result->at_phent;
        *p++ = AT_PHNUM; *p++ = (uint64_t)result->at_phnum;
        *p++ = AT_PAGESZ; *p++ = PAGE_SIZE;
        *p++ = AT_BASE;  *p++ = result->at_base;
        *p++ = AT_FLAGS; *p++ = 0;
        *p++ = AT_ENTRY; *p++ = result->at_entry;
        *p++ = AT_NULL;  *p++ = 0;
    }

    *rsp_out      = stack_base_virt + (uint64_t)start;
    *argv_ptr_out = stack_base_virt + (uint64_t)start + 8;
    return 0;
}
