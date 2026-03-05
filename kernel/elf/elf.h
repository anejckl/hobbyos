#ifndef ELF_H
#define ELF_H

#include "../common.h"

/* ELF magic */
#define ELF_MAGIC0 0x7F
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

/* e_ident indices */
#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_NIDENT   16

/* EI_CLASS values */
#define ELFCLASS64  2

/* EI_DATA values */
#define ELFDATA2LSB 1  /* Little-endian */

/* e_type values */
#define ET_EXEC     2  /* Executable */

/* e_machine values */
#define EM_X86_64   62

/* ELF version */
#define EV_CURRENT  1

/* Program header types */
#define PT_NULL     0
#define PT_LOAD     1

/* Program header flags */
#define PF_X        0x1  /* Execute */
#define PF_W        0x2  /* Write */
#define PF_R        0x4  /* Read */

/* ELF64 file header */
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

/* ELF64 program header */
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

#endif /* ELF_H */
