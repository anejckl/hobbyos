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
#define ET_DYN      3  /* Shared object */

/* e_machine values */
#define EM_X86_64   62

/* ELF version */
#define EV_CURRENT  1

/* Program header types */
#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2  /* Dynamic linking info */
#define PT_INTERP   3  /* Interpreter path */
#define PT_PHDR     6  /* Program header table */

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

/* Dynamic section tags */
#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9
#define DT_SONAME   14
#define DT_RPATH    15
#define DT_PLTREL   20
#define DT_JMPREL   23

/* Relocation types */
#define R_X86_64_NONE       0
#define R_X86_64_64         1
#define R_X86_64_GLOB_DAT   6
#define R_X86_64_JUMP_SLOT  7
#define R_X86_64_RELATIVE   8

/* Auxiliary vector types */
#define AT_NULL     0
#define AT_PHDR     3
#define AT_PHENT    4
#define AT_PHNUM    5
#define AT_PAGESZ   6
#define AT_BASE     7
#define AT_FLAGS    8
#define AT_ENTRY    9

/* ELF64 dynamic section entry */
typedef struct {
    int64_t  d_tag;
    uint64_t d_val;
} __attribute__((packed)) Elf64_Dyn;

/* ELF64 symbol table entry */
typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} __attribute__((packed)) Elf64_Sym;

/* ELF64 relocation with addend */
typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} __attribute__((packed)) Elf64_Rela;

#define ELF64_R_TYPE(info)  ((uint32_t)(info))
#define ELF64_R_SYM(info)   ((uint32_t)((info) >> 32))

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
