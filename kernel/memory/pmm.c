#include "pmm.h"
#include "../string.h"
#include "../drivers/vga.h"
#include "../debug/debug.h"

/* Multiboot2 structures */
struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;      /* 1 = available, others = reserved */
    uint32_t reserved;
} __attribute__((packed));

struct mb2_mmap_tag {
    uint32_t type;      /* 6 */
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct mb2_mmap_entry entries[];
} __attribute__((packed));

/* Linker symbols */
extern char _kernel_phys_end[];

/* Bitmap allocator */
static uint8_t *bitmap;
static uint64_t bitmap_size;        /* Size in bytes */
static uint64_t total_pages;
static uint64_t free_pages;
static uint64_t max_phys_addr;

/* Per-page reference counts (for COW) */
static uint8_t *page_refcount;

static inline void bitmap_set(uint64_t page) {
    bitmap[page / 8] |= (1 << (page % 8));
}

static inline void bitmap_clear(uint64_t page) {
    bitmap[page / 8] &= ~(1 << (page % 8));
}

static inline bool bitmap_test(uint64_t page) {
    return bitmap[page / 8] & (1 << (page % 8));
}

void pmm_init(uint32_t multiboot_info_phys) {
    /* Access multiboot2 info via physical memory map */
    uint8_t *mb_info = (uint8_t *)PHYS_TO_VIRT(multiboot_info_phys);
    uint32_t mb_total_size = *(uint32_t *)mb_info;

    /* First pass: find max physical address from memory map */
    max_phys_addr = 0;
    uint8_t *tag_ptr = mb_info + 8;  /* Skip total_size and reserved */
    uint8_t *mb_end = mb_info + mb_total_size;

    while (tag_ptr < mb_end) {
        struct mb2_tag *tag = (struct mb2_tag *)tag_ptr;
        if (tag->type == 0)  /* End tag */
            break;

        if (tag->type == 6) {  /* Memory map */
            struct mb2_mmap_tag *mmap = (struct mb2_mmap_tag *)tag;
            uint8_t *entry_ptr = (uint8_t *)mmap->entries;
            uint8_t *entries_end = (uint8_t *)tag + tag->size;

            while (entry_ptr < entries_end) {
                struct mb2_mmap_entry *entry = (struct mb2_mmap_entry *)entry_ptr;
                uint64_t end = entry->base_addr + entry->length;
                if (end > max_phys_addr)
                    max_phys_addr = end;
                entry_ptr += mmap->entry_size;
            }
        }

        /* Tags are 8-byte aligned */
        tag_ptr += (tag->size + 7) & ~7;
    }

    /* Cap at 512MB (our physical memory map size) */
    if (max_phys_addr > 512ULL * 1024 * 1024)
        max_phys_addr = 512ULL * 1024 * 1024;

    /* Calculate bitmap parameters */
    total_pages = max_phys_addr / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8;

    /* Place bitmap right after kernel physical end */
    uint64_t bitmap_phys = (uint64_t)_kernel_phys_end;
    bitmap_phys = (bitmap_phys + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  /* Page-align */
    bitmap = (uint8_t *)PHYS_TO_VIRT(bitmap_phys);

    /* Place refcount array right after bitmap (page-aligned) */
    uint64_t refcount_phys = bitmap_phys + bitmap_size;
    refcount_phys = (refcount_phys + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    page_refcount = (uint8_t *)PHYS_TO_VIRT(refcount_phys);

    /* Mark ALL pages as used initially */
    memset(bitmap, 0xFF, bitmap_size);
    memset(page_refcount, 0, total_pages);
    free_pages = 0;

    /* Second pass: free available regions from memory map */
    tag_ptr = mb_info + 8;
    while (tag_ptr < mb_end) {
        struct mb2_tag *tag = (struct mb2_tag *)tag_ptr;
        if (tag->type == 0)
            break;

        if (tag->type == 6) {
            struct mb2_mmap_tag *mmap = (struct mb2_mmap_tag *)tag;
            uint8_t *entry_ptr = (uint8_t *)mmap->entries;
            uint8_t *entries_end = (uint8_t *)tag + tag->size;

            while (entry_ptr < entries_end) {
                struct mb2_mmap_entry *entry = (struct mb2_mmap_entry *)entry_ptr;

                if (entry->type == 1) {  /* Available memory */
                    uint64_t base = entry->base_addr;
                    uint64_t end = base + entry->length;

                    /* Page-align: round base up, end down */
                    base = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                    end &= ~(PAGE_SIZE - 1);

                    for (uint64_t addr = base; addr < end && addr < max_phys_addr; addr += PAGE_SIZE) {
                        uint64_t page = addr / PAGE_SIZE;
                        if (bitmap_test(page)) {
                            bitmap_clear(page);
                            free_pages++;
                        }
                    }
                }
                entry_ptr += mmap->entry_size;
            }
        }

        tag_ptr += (tag->size + 7) & ~7;
    }

    /* Re-reserve: first 1MB (BIOS/legacy), kernel, bitmap */
    for (uint64_t addr = 0; addr < 0x100000; addr += PAGE_SIZE) {
        uint64_t page = addr / PAGE_SIZE;
        if (!bitmap_test(page)) {
            bitmap_set(page);
            free_pages--;
        }
    }

    /* Reserve kernel + bitmap + refcount region */
    uint64_t reserve_end = refcount_phys + total_pages;
    reserve_end = (reserve_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t addr = 0x100000; addr < reserve_end; addr += PAGE_SIZE) {
        uint64_t page = addr / PAGE_SIZE;
        if (!bitmap_test(page)) {
            bitmap_set(page);
            free_pages--;
        }
    }

    debug_printf("PMM: max_phys=0x%x total_pages=%u free_pages=%u bitmap_at=0x%x\n",
                 max_phys_addr, total_pages, free_pages, bitmap_phys);
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            page_refcount[i] = 1;
            return i * PAGE_SIZE;
        }
    }
    return 0;  /* Out of memory */
}

void pmm_free_page(uint64_t phys_addr) {
    uint64_t page = phys_addr / PAGE_SIZE;
    if (page < total_pages && bitmap_test(page)) {
        bitmap_clear(page);
        page_refcount[page] = 0;
        free_pages++;
    }
}

uint64_t pmm_get_free_pages(void) {
    return free_pages;
}

uint64_t pmm_get_total_pages(void) {
    return total_pages;
}

void pmm_page_ref(uint64_t phys_addr) {
    uint64_t page = phys_addr / PAGE_SIZE;
    if (page < total_pages && page_refcount[page] < 255)
        page_refcount[page]++;
}

void pmm_page_unref(uint64_t phys_addr) {
    uint64_t page = phys_addr / PAGE_SIZE;
    if (page < total_pages && page_refcount[page] > 0) {
        page_refcount[page]--;
        if (page_refcount[page] == 0) {
            bitmap_clear(page);
            free_pages++;
        }
    }
}

uint8_t pmm_page_refcount(uint64_t phys_addr) {
    uint64_t page = phys_addr / PAGE_SIZE;
    if (page < total_pages)
        return page_refcount[page];
    return 0;
}
