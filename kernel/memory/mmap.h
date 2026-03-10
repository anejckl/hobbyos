#ifndef MMAP_H
#define MMAP_H

#include "../common.h"

/* mmap region: 0x10000000 - 0x7FC0000000 */
#define MMAP_BASE      0x10000000ULL
#define MMAP_TOP       0x7FC0000000ULL

#define VMA_MAX        16

/* VMA types */
#define VMA_ANON       0
#define VMA_FILE       1

/* mmap prot flags */
#define PROT_NONE      0
#define PROT_READ      1
#define PROT_WRITE     2
#define PROT_EXEC      4

/* mmap flags */
#define MAP_SHARED     1
#define MAP_PRIVATE    2
#define MAP_ANONYMOUS  0x20
#define MAP_FIXED      0x10
#define MAP_FAILED     ((void *)-1)

/* 6-arg mmap via struct pointer */
struct mmap_args {
    uint64_t addr;
    uint64_t len;
    uint32_t prot;
    uint32_t flags;
    int32_t  fd;
    uint32_t pad;
    uint64_t offset;
};

typedef struct {
    uint64_t start;         /* page-aligned */
    uint64_t end;           /* page-aligned, exclusive */
    uint32_t prot;
    uint32_t flags;
    uint8_t  type;          /* VMA_ANON or VMA_FILE */
    bool     in_use;
    uint32_t file_inode;    /* ext2 inode number (VMA_FILE only) */
    uint64_t file_offset;   /* offset into file */
    uint64_t shared_phys;   /* MAP_SHARED anon: shared physical page */
} vma_t;

struct process; /* forward decl */

/* Find VMA containing addr */
vma_t *vma_find(struct process *proc, uint64_t addr);

/* Allocate a free VMA slot */
vma_t *vma_alloc(struct process *proc);

/* Find free virtual range in mmap region */
uint64_t vma_find_free_range(struct process *proc, uint64_t hint, uint64_t len);

/* Handle a page fault in mmap region. Returns 0 on success, -1 if no VMA. */
int vma_handle_fault(struct process *proc, uint64_t fault_addr, bool is_write);

/* Unmap a range. Returns 0 on success. */
int vma_unmap(struct process *proc, uint64_t addr, uint64_t len);

/* Change protection of a range. */
int vma_protect(struct process *proc, uint64_t addr, uint64_t len, uint32_t prot);

/* Copy VMA array from parent to child (for fork). */
void vma_fork_copy(struct process *parent, struct process *child);

/* Clear all VMAs (for exec/exit). Physical pages freed by user_vm_destroy_address_space. */
void vma_destroy_all(struct process *proc);

#endif /* MMAP_H */
