/*
 * test_elf.c — Unit tests for ELF64 header validation.
 *
 * Tests elf_validate() by including elf_loader.c with mocked dependencies.
 * Uses macro renaming to avoid symbol conflicts with other test files.
 */

#include "test_main.h"
#include <string.h>

#include "stubs.h"

/* ELF constants (duplicated from kernel/elf/elf.h for host test) */
#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_NIDENT   16

#define ELF_MAGIC0  0x7F
#define ELF_MAGIC1  'E'
#define ELF_MAGIC2  'L'
#define ELF_MAGIC3  'F'
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define EM_X86_64   62
#define EV_CURRENT  1
#define PT_LOAD     1
#define PF_X        0x1
#define PF_W        0x2
#define PF_R        0x4

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) Elf64_Phdr;

/* Rename conflicting symbols before including elf_loader.c */
#define pmm_alloc_page    elf_test_pmm_alloc_page
#define user_vm_map_page  elf_test_user_vm_map_page
#define debug_printf      elf_test_debug_printf

static uint64_t elf_test_alloc_counter = 0;
/* Allocate real pages via malloc for host-side testing */
static uint8_t elf_test_pages[16][4096];

static uint64_t elf_test_pmm_alloc_page(void) {
    if (elf_test_alloc_counter >= 16)
        return 0;
    uint64_t addr = (uint64_t)(uintptr_t)elf_test_pages[elf_test_alloc_counter];
    elf_test_alloc_counter++;
    return addr;
}

static int elf_test_user_vm_map_page(uint64_t pml4_phys, uint64_t virt,
                                     uint64_t phys, uint64_t flags) {
    (void)pml4_phys; (void)virt; (void)phys; (void)flags;
    return 0;
}

static void elf_test_debug_printf(const char *fmt, ...) { (void)fmt; }

/* Block headers that elf_loader.c would include */
/* Provide elf_load_result struct (from elf_loader.h) */
struct elf_load_result {
    uint64_t entry_point;
};

#define COMMON_H
#define ELF_H
#define ELF_LOADER_H
#define PMM_H
#define VMM_H
#define USER_VM_H
#define STRING_H
#define DEBUG_H

/* Provide PTE flags needed by elf_loader.c */
#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITABLE (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_NX       (1ULL << 63)

/* Include the actual elf_loader.c source */
#include "../kernel/elf/elf_loader.c"

#undef pmm_alloc_page
#undef user_vm_map_page
#undef debug_printf

/* Helper to build a minimal valid ELF64 header + program header */
static void make_valid_elf(uint8_t *buf, size_t bufsize) {
    memset(buf, 0, bufsize);

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
    ehdr->e_ident[EI_MAG0] = ELF_MAGIC0;
    ehdr->e_ident[EI_MAG1] = ELF_MAGIC1;
    ehdr->e_ident[EI_MAG2] = ELF_MAGIC2;
    ehdr->e_ident[EI_MAG3] = ELF_MAGIC3;
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_ident[EI_VERSION] = EV_CURRENT;
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_X86_64;
    ehdr->e_version = EV_CURRENT;
    ehdr->e_entry = 0x400000;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_ehsize = sizeof(Elf64_Ehdr);
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_phnum = 1;

    Elf64_Phdr *phdr = (Elf64_Phdr *)(buf + sizeof(Elf64_Ehdr));
    phdr->p_type = PT_LOAD;
    phdr->p_flags = PF_R | PF_X;
    phdr->p_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    phdr->p_vaddr = 0x400000;
    phdr->p_paddr = 0x400000;
    phdr->p_filesz = 16;
    phdr->p_memsz = 16;
    phdr->p_align = 0x1000;
}

static void test_elf_validate_valid(void) {
    uint8_t buf[512];
    make_valid_elf(buf, sizeof(buf));
    TEST("valid ELF passes validation", elf_validate(buf, sizeof(buf)) == 0);
}

static void test_elf_validate_null(void) {
    TEST("NULL data fails", elf_validate(NULL, 100) == -1);
}

static void test_elf_validate_too_small(void) {
    uint8_t buf[8] = {0};
    TEST("too-small buffer fails", elf_validate(buf, sizeof(buf)) == -1);
}

static void test_elf_validate_bad_magic(void) {
    uint8_t buf[512];
    make_valid_elf(buf, sizeof(buf));
    buf[0] = 0x00;
    TEST("bad magic fails", elf_validate(buf, sizeof(buf)) == -1);
}

static void test_elf_validate_bad_class(void) {
    uint8_t buf[512];
    make_valid_elf(buf, sizeof(buf));
    buf[EI_CLASS] = 1;
    TEST("32-bit class fails", elf_validate(buf, sizeof(buf)) == -1);
}

static void test_elf_validate_bad_endian(void) {
    uint8_t buf[512];
    make_valid_elf(buf, sizeof(buf));
    buf[EI_DATA] = 2;
    TEST("big-endian fails", elf_validate(buf, sizeof(buf)) == -1);
}

static void test_elf_validate_bad_type(void) {
    uint8_t buf[512];
    make_valid_elf(buf, sizeof(buf));
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
    ehdr->e_type = 3;
    TEST("non-exec type fails", elf_validate(buf, sizeof(buf)) == -1);
}

static void test_elf_validate_bad_machine(void) {
    uint8_t buf[512];
    make_valid_elf(buf, sizeof(buf));
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
    ehdr->e_machine = 3;
    TEST("wrong machine fails", elf_validate(buf, sizeof(buf)) == -1);
}

static void test_elf_validate_no_phdr(void) {
    uint8_t buf[512];
    make_valid_elf(buf, sizeof(buf));
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
    ehdr->e_phnum = 0;
    TEST("no program headers fails", elf_validate(buf, sizeof(buf)) == -1);
}

static void test_elf_validate_phdr_oob(void) {
    uint8_t buf[512];
    make_valid_elf(buf, sizeof(buf));
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
    ehdr->e_phoff = 500;
    TEST("phdr out of bounds fails", elf_validate(buf, sizeof(buf)) == -1);
}

static void test_elf_load_entry_point(void) {
    uint8_t buf[512];
    make_valid_elf(buf, sizeof(buf));

    elf_test_alloc_counter = 0;

    struct elf_load_result result;
    int ret = elf_load(0x1000, buf, sizeof(buf), &result);
    TEST("elf_load succeeds", ret == 0);
    TEST("entry point is 0x400000", result.entry_point == 0x400000);
}

void test_elf_suite(void) {
    printf("=== ELF loader tests ===\n");
    test_elf_validate_valid();
    test_elf_validate_null();
    test_elf_validate_too_small();
    test_elf_validate_bad_magic();
    test_elf_validate_bad_class();
    test_elf_validate_bad_endian();
    test_elf_validate_bad_type();
    test_elf_validate_bad_machine();
    test_elf_validate_no_phdr();
    test_elf_validate_phdr_oob();
    test_elf_load_entry_point();
}
