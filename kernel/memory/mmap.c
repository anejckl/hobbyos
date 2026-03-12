#include "mmap.h"
#include "pmm.h"
#include "vmm.h"
#include "user_vm.h"
#include "kheap.h"
#include "../process/process.h"
#include "../fs/ext2.h"
#include "../string.h"
#include "../debug/debug.h"

vma_t *vma_find(struct process *proc, uint64_t addr) {
    for (int i = 0; i < VMA_MAX; i++) {
        if (proc->vmas[i].in_use &&
            addr >= proc->vmas[i].start &&
            addr <  proc->vmas[i].end)
            return &proc->vmas[i];
    }
    return NULL;
}

vma_t *vma_alloc(struct process *proc) {
    for (int i = 0; i < VMA_MAX; i++) {
        if (!proc->vmas[i].in_use)
            return &proc->vmas[i];
    }
    return NULL;
}

uint64_t vma_find_free_range(struct process *proc, uint64_t hint, uint64_t len) {
    uint64_t base = (hint && hint >= MMAP_BASE && hint < MMAP_TOP) ? hint : proc->mmap_next;
    /* Align to page */
    base = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

retry:
    if (base + len > MMAP_TOP)
        return 0;

    /* Check no existing VMA overlaps [base, base+len) */
    for (int i = 0; i < VMA_MAX; i++) {
        if (!proc->vmas[i].in_use) continue;
        uint64_t s = proc->vmas[i].start;
        uint64_t e = proc->vmas[i].end;
        if (base < e && base + len > s) {
            base = e;
            goto retry;
        }
    }

    return base;
}

int vma_handle_fault(struct process *proc, uint64_t fault_addr, bool is_write) {
    vma_t *vma = vma_find(proc, fault_addr);
    if (!vma)
        return -1;

    /* Check write permission */
    if (is_write && !(vma->prot & PROT_WRITE))
        return -1;

    /* Page-align fault address */
    uint64_t page = fault_addr & ~(PAGE_SIZE - 1);

    /* MAP_SHARED anonymous: use/create shared physical page */
    if ((vma->flags & MAP_SHARED) && vma->type == VMA_ANON) {
        uint64_t phys;
        if (vma->shared_phys) {
            phys = vma->shared_phys;
        } else {
            phys = pmm_alloc_page();
            if (!phys) return -1;
            memset((void *)PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
            vma->shared_phys = phys;
        }
        uint64_t flags = PTE_PRESENT | PTE_USER;
        if (vma->prot & PROT_WRITE) flags |= PTE_WRITABLE;
        return user_vm_map_page(proc->cr3, page, phys, flags);
    }

    /* Allocate fresh page */
    uint64_t phys = pmm_alloc_page();
    if (!phys) return -1;
    memset((void *)PHYS_TO_VIRT(phys), 0, PAGE_SIZE);

    /* Device-backed: map physical device pages directly */
    if (vma->type == VMA_DEVICE) {
        uint64_t page_offset = page - vma->start;
        uint64_t dev_phys = vma->dev_phys_base + page_offset;
        uint64_t dflags = PTE_PRESENT | PTE_USER | PTE_NOCACHE;
        if (vma->prot & PROT_WRITE) dflags |= PTE_WRITABLE;
        /* Free the fresh page we allocated above since we won't use it */
        pmm_page_unref(phys);
        return user_vm_map_page(proc->cr3, page, dev_phys, dflags);
    }

    /* File-backed: read file data into page */
    if (vma->type == VMA_FILE) {
        struct ext2_inode inode;
        if (ext2_read_inode(vma->file_inode, &inode) == 0) {
            uint64_t file_page_off = vma->file_offset + (page - vma->start);
            ext2_read_file(&inode, file_page_off, PAGE_SIZE,
                           (uint8_t *)PHYS_TO_VIRT(phys));
        }
    }

    /* ELF demand paging: copy file data from in-memory ELF */
    if (vma->type == VMA_ELF && vma->elf_data) {
        uint64_t page_offset = page - vma->elf_vaddr;
        uint8_t *kpage = (uint8_t *)PHYS_TO_VIRT(phys);
        if (page_offset < vma->elf_data_filesz) {
            uint64_t copy_len = vma->elf_data_filesz - page_offset;
            if (copy_len > PAGE_SIZE)
                copy_len = PAGE_SIZE;
            memcpy(kpage, vma->elf_data + page_offset, copy_len);
        }
    }

    uint64_t flags = PTE_PRESENT | PTE_USER;
    if (vma->prot & PROT_WRITE) flags |= PTE_WRITABLE;

    return user_vm_map_page(proc->cr3, page, phys, flags);
}

int vma_unmap(struct process *proc, uint64_t addr, uint64_t len) {
    uint64_t end = (addr + len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    addr = addr & ~(PAGE_SIZE - 1);

    for (int i = 0; i < VMA_MAX; i++) {
        vma_t *v = &proc->vmas[i];
        if (!v->in_use) continue;
        if (v->start >= end || v->end <= addr) continue;

        /* Unmap PTEs */
        for (uint64_t pg = v->start; pg < v->end; pg += PAGE_SIZE) {
            uint64_t *pte = user_vm_get_pte(proc->cr3, pg);
            if (pte && (*pte & PTE_PRESENT)) {
                uint64_t phys = *pte & 0x000FFFFFFFFFF000ULL;
                pmm_page_unref(phys);
                *pte = 0;
            }
        }
        v->in_use = false;
    }
    return 0;
}

int vma_protect(struct process *proc, uint64_t addr, uint64_t len, uint32_t prot) {
    uint64_t end = (addr + len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    addr = addr & ~(PAGE_SIZE - 1);

    for (int i = 0; i < VMA_MAX; i++) {
        vma_t *v = &proc->vmas[i];
        if (!v->in_use) continue;
        if (v->start >= end || v->end <= addr) continue;
        v->prot = prot;

        /* Update existing PTEs */
        for (uint64_t pg = v->start; pg < v->end; pg += PAGE_SIZE) {
            uint64_t *pte = user_vm_get_pte(proc->cr3, pg);
            if (pte && (*pte & PTE_PRESENT)) {
                uint64_t phys = *pte & 0x000FFFFFFFFFF000ULL;
                uint64_t flags = PTE_PRESENT | PTE_USER;
                if (prot & PROT_WRITE) flags |= PTE_WRITABLE;
                *pte = phys | flags;
            }
        }
    }
    return 0;
}

void vma_fork_copy(struct process *parent, struct process *child) {
    for (int i = 0; i < VMA_MAX; i++)
        child->vmas[i] = parent->vmas[i];
    child->mmap_next = parent->mmap_next;
    child->brk_start = parent->brk_start;
    child->brk_current = parent->brk_current;
}

void vma_destroy_all(struct process *proc) {
    for (int i = 0; i < VMA_MAX; i++) {
        if (proc->vmas[i].in_use && proc->vmas[i].type == VMA_ELF &&
            proc->vmas[i].elf_data_owned && proc->vmas[i].elf_data) {
            kfree((void *)proc->vmas[i].elf_data);
        }
        proc->vmas[i].in_use = false;
    }
    proc->mmap_next = MMAP_BASE;
}
