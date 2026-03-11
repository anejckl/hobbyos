/*
 * ld.so — minimal user-space dynamic linker for HobbyOS
 *
 * Loaded at LDSO_BASE (0x7FC0000000) by the kernel when a program has PT_INTERP.
 * Entry is _ldso_start (asm below), which calls ldso_main(sp).
 *
 * Stack layout on entry (as set up by kernel):
 *   [rsp+0]   argc
 *   [rsp+8]   argv[0] ... argv[argc-1]
 *   [rsp+8+argc*8] NULL
 *   envp[0]...NULL
 *   auxv: {AT_PHDR, val}, {AT_PHENT, val}, {AT_PHNUM, val},
 *         {AT_PAGESZ, val}, {AT_BASE, ld.so base}, {AT_FLAGS, 0},
 *         {AT_ENTRY, main-binary entry}, {AT_NULL, 0}
 */

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed int         int32_t;
typedef long long          int64_t;
typedef uint64_t           size_t;
#define NULL ((void *)0)

/* Syscall numbers */
#define SYS_WRITE    0
#define SYS_EXIT     1
#define SYS_OPEN     7
#define SYS_CLOSE    8
#define SYS_MMAP     27
#define SYS_MUNMAP   28
#define SYS_BRK      30

/* mmap flags */
#define PROT_READ    1
#define PROT_WRITE   2
#define PROT_EXEC    4
#define MAP_SHARED   1
#define MAP_PRIVATE  2
#define MAP_ANONYMOUS 0x20

/* Auxv types */
#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_ENTRY  9

/* ELF types */
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PF_W       0x2
#define PF_X       0x1

#define DT_NULL    0
#define DT_NEEDED  1
#define DT_STRTAB  5
#define DT_SYMTAB  6
#define DT_RELA    7
#define DT_RELASZ  8
#define DT_RELAENT 9
#define DT_JMPREL  23
#define DT_PLTRELSZ 2

#define R_X86_64_NONE      0
#define R_X86_64_64        1
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8

#define ELF64_R_TYPE(i)  ((uint32_t)(i))
#define ELF64_R_SYM(i)   ((uint32_t)((i) >> 32))

typedef struct {
    uint8_t  e_ident[16];
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

typedef struct {
    int64_t  d_tag;
    uint64_t d_val;
} __attribute__((packed)) Elf64_Dyn;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} __attribute__((packed)) Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} __attribute__((packed)) Elf64_Rela;

/* ---- Inline syscalls ---- */
static inline uint64_t sc0(uint64_t n) {
    uint64_t r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n):"rcx","r11","memory");
    return r;
}
static inline uint64_t sc1(uint64_t n, uint64_t a) {
    uint64_t r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"D"(a):"rcx","r11","memory");
    return r;
}
static inline uint64_t sc2(uint64_t n, uint64_t a, uint64_t b) {
    uint64_t r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"D"(a),"S"(b):"rcx","r11","memory");
    return r;
}
static inline uint64_t sc3(uint64_t n, uint64_t a, uint64_t b, uint64_t c) {
    uint64_t r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");
    return r;
}

/* ---- Memory ---- */
static void ldso_memset(void *p, int c, size_t n) {
    uint8_t *d = (uint8_t *)p;
    while (n--) *d++ = (uint8_t)c;
}
static void ldso_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}
static size_t ldso_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
static int ldso_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* mmap_args struct (matches kernel) */
struct mmap_args_s {
    uint64_t addr; uint64_t len;
    uint32_t prot; uint32_t flags;
    int32_t  fd;   uint32_t pad;
    uint64_t offset;
};

static void *ldso_mmap(void *addr, size_t len, uint32_t prot,
                        uint32_t flags, int32_t fd, uint64_t off) {
    struct mmap_args_s a;
    a.addr = (uint64_t)addr; a.len = len;
    a.prot = prot; a.flags = flags;
    a.fd = fd; a.pad = 0; a.offset = off;
    return (void *)sc1(SYS_MMAP, (uint64_t)&a);
}

static void ldso_write(const char *s) {
    sc3(SYS_WRITE, 1, (uint64_t)s, ldso_strlen(s));
}

__attribute__((noreturn))
static void ldso_die(const char *msg) {
    ldso_write("ld.so: ");
    ldso_write(msg);
    ldso_write("\n");
    sc1(SYS_EXIT, 1);
    for (;;);
}

/* ---- Symbol table ---- */
#define MAX_SYMS  256
struct sym_entry { const char *name; uint64_t addr; };
static struct sym_entry sym_table[MAX_SYMS];
static int sym_count = 0;

static void sym_add(const char *name, uint64_t addr) {
    if (sym_count >= MAX_SYMS) return;
    sym_table[sym_count].name = name;
    sym_table[sym_count].addr = addr;
    sym_count++;
}

static uint64_t sym_lookup(const char *name) {
    for (int i = 0; i < sym_count; i++)
        if (ldso_strcmp(sym_table[i].name, name) == 0)
            return sym_table[i].addr;
    return 0;
}

/* ---- Apply relocations for a loaded library ---- */
static void apply_rela(uint64_t base, uint64_t rela_off, uint64_t rela_sz,
                        uint64_t symtab_off, uint64_t strtab_off) {
    if (!rela_off || !rela_sz) return;
    uint64_t count = rela_sz / sizeof(Elf64_Rela);
    Elf64_Rela *relas = (Elf64_Rela *)(base + rela_off);
    Elf64_Sym  *syms  = symtab_off ? (Elf64_Sym *)(base + symtab_off) : NULL;
    const char *strtab = strtab_off ? (const char *)(base + strtab_off) : NULL;

    for (uint64_t i = 0; i < count; i++) {
        uint64_t *slot = (uint64_t *)(base + relas[i].r_offset);
        uint32_t type  = ELF64_R_TYPE(relas[i].r_info);
        uint32_t sym_i = ELF64_R_SYM(relas[i].r_info);

        if (type == R_X86_64_RELATIVE) {
            *slot = base + (uint64_t)relas[i].r_addend;
        } else if ((type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT ||
                    type == R_X86_64_64) && syms && strtab && sym_i) {
            const char *sname = strtab + syms[sym_i].st_name;
            uint64_t addr = sym_lookup(sname);
            if (!addr && syms[sym_i].st_value)
                addr = base + syms[sym_i].st_value;
            if (addr) {
                if (type == R_X86_64_64)
                    *slot = addr + (uint64_t)relas[i].r_addend;
                else
                    *slot = addr;
            }
        }
    }
}

/* ---- Self-relocation (no globals accessible yet) ---- */
static void self_relocate(uint64_t base) {
    /* Walk program headers to find PT_DYNAMIC */
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)base;
    Elf64_Phdr *phdrs = (Elf64_Phdr *)(base + ehdr->e_phoff);

    uint64_t rela_off = 0, rela_sz = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_DYNAMIC) continue;
        Elf64_Dyn *dyn = (Elf64_Dyn *)(base + phdrs[i].p_vaddr);
        for (; dyn->d_tag != DT_NULL; dyn++) {
            if (dyn->d_tag == DT_RELA)    rela_off = dyn->d_val;
            if (dyn->d_tag == DT_RELASZ)  rela_sz  = dyn->d_val;
        }
        break;
    }

    if (!rela_off || !rela_sz) return;
    uint64_t count = rela_sz / sizeof(Elf64_Rela);
    Elf64_Rela *relas = (Elf64_Rela *)(base + rela_off);
    for (uint64_t i = 0; i < count; i++) {
        if (ELF64_R_TYPE(relas[i].r_info) == R_X86_64_RELATIVE) {
            uint64_t *slot = (uint64_t *)(base + relas[i].r_offset);
            *slot = base + (uint64_t)relas[i].r_addend;
        }
    }
}

/* ---- Load a shared library from ext2 ---- */
#define MAX_LIBS 8
static uint64_t loaded_bases[MAX_LIBS];
static int loaded_count = 0;

/* Current library load address hint */
static uint64_t next_lib_base = 0x20000000ULL;
#define PAGE_SIZE 4096ULL

static uint64_t load_library(const char *path) {
    /* Open file */
    int fd = (int)sc2(SYS_OPEN, (uint64_t)path, 0);
    if (fd < 0) return 0;

    /* Read ELF header */
    Elf64_Ehdr ehdr;
    /* We need to read the file — use mmap with FD */
    /* First get file size via a temp read approach:
     * Try to mmap a large region then trim — simpler: just mmap 16MB */
    uint64_t file_size = 16 * 1024 * 1024ULL; /* pessimistic */
    struct mmap_args_s ma;
    ma.addr = 0; ma.len = file_size;
    ma.prot = PROT_READ; ma.flags = MAP_PRIVATE;
    ma.fd = fd; ma.pad = 0; ma.offset = 0;
    uint8_t *fbuf = (uint8_t *)sc1(SYS_MMAP, (uint64_t)&ma);
    sc1(SYS_CLOSE, (uint64_t)fd);

    if ((uint64_t)fbuf == (uint64_t)-1) return 0;

    /* Validate ELF */
    Elf64_Ehdr *fhdr = (Elf64_Ehdr *)fbuf;
    if (fhdr->e_ident[0] != 0x7f || fhdr->e_ident[1] != 'E') return 0;

    /* Compute load range from PT_LOAD segments */
    Elf64_Phdr *phdrs = (Elf64_Phdr *)(fbuf + fhdr->e_phoff);
    uint64_t min_vaddr = (uint64_t)-1, max_vaddr = 0;
    for (int i = 0; i < fhdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        uint64_t s = phdrs[i].p_vaddr & ~(PAGE_SIZE-1);
        uint64_t e = (phdrs[i].p_vaddr + phdrs[i].p_memsz + PAGE_SIZE-1) & ~(PAGE_SIZE-1);
        if (s < min_vaddr) min_vaddr = s;
        if (e > max_vaddr) max_vaddr = e;
    }
    if (min_vaddr == (uint64_t)-1) return 0;

    /* Allocate anonymous region */
    uint64_t map_size = max_vaddr - min_vaddr;
    ma.addr = next_lib_base; ma.len = map_size;
    ma.prot = PROT_READ | PROT_WRITE; ma.flags = MAP_PRIVATE | MAP_ANONYMOUS;
    ma.fd = -1; ma.pad = 0; ma.offset = 0;
    uint8_t *map = (uint8_t *)sc1(SYS_MMAP, (uint64_t)&ma);
    if ((uint64_t)map == (uint64_t)-1) return 0;

    uint64_t base = (uint64_t)map - min_vaddr;
    next_lib_base = ((uint64_t)map + map_size + PAGE_SIZE - 1) & ~(PAGE_SIZE-1);

    /* Copy PT_LOAD segments */
    for (int i = 0; i < fhdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        uint8_t *dst = (uint8_t *)(base + phdrs[i].p_vaddr);
        ldso_memset(dst, 0, phdrs[i].p_memsz);
        ldso_memcpy(dst, fbuf + phdrs[i].p_offset, phdrs[i].p_filesz);
    }

    /* Process PT_DYNAMIC to extract symbol table entries */
    uint64_t strtab_off = 0, symtab_off = 0;
    uint64_t rela_off = 0, rela_sz = 0;
    uint64_t jmprel_off = 0, jmprel_sz = 0;

    for (int i = 0; i < fhdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_DYNAMIC) continue;
        Elf64_Dyn *dyn = (Elf64_Dyn *)(base + phdrs[i].p_vaddr);
        for (; dyn->d_tag != DT_NULL; dyn++) {
            switch ((int)dyn->d_tag) {
            case DT_STRTAB:   strtab_off  = dyn->d_val; break;
            case DT_SYMTAB:   symtab_off  = dyn->d_val; break;
            case DT_RELA:     rela_off    = dyn->d_val; break;
            case DT_RELASZ:   rela_sz     = dyn->d_val; break;
            case DT_JMPREL:   jmprel_off  = dyn->d_val; break;
            case DT_PLTRELSZ: jmprel_sz   = dyn->d_val; break;
            }
        }
        break;
    }

    /* Register defined symbols */
    if (symtab_off && strtab_off) {
        const char *strtab = (const char *)(base + strtab_off);
        Elf64_Sym *syms = (Elf64_Sym *)(base + symtab_off);
        /* Walk until we hit null entries — stop at reasonable bound */
        for (int i = 1; i < 4096; i++) {
            if (!syms[i].st_name && !syms[i].st_value) break;
            if (syms[i].st_shndx == 0) continue; /* undefined */
            if (syms[i].st_value == 0) continue;
            sym_add(strtab + syms[i].st_name, base + syms[i].st_value);
        }
    }

    /* Apply relocations */
    apply_rela(base, rela_off,    rela_sz,    symtab_off, strtab_off);
    apply_rela(base, jmprel_off,  jmprel_sz,  symtab_off, strtab_off);

    /* Unmap file */
    sc2(SYS_MUNMAP, (uint64_t)fbuf, file_size);

    if (loaded_count < MAX_LIBS)
        loaded_bases[loaded_count++] = base;

    return base;
}

/* ---- Main dynamic linker entry ---- */
void ldso_main(uint64_t *sp) {
    /* 1. Parse stack: argc, argv[], NULL, envp[], NULL, auxv */
    uint64_t argc = *sp;
    (void)argc;
    /* skip argc + argv + NULL + envp + NULL */
    uint64_t *p = sp + 1;
    while (*p) p++;  /* skip argv */
    p++;             /* skip NULL */
    while (*p) p++;  /* skip envp */
    p++;             /* skip NULL */

    /* 2. Read auxv */
    uint64_t at_phdr = 0, at_phnum = 0, at_base = 0, at_entry = 0;
    uint64_t at_pagesz = 4096;
    uint64_t *av = p;
    while (*av != AT_NULL) {
        uint64_t tag = av[0], val = av[1];
        switch (tag) {
        case AT_PHDR:  at_phdr  = val; break;
        case AT_PHNUM: at_phnum = val; break;
        case AT_BASE:  at_base  = val; break;
        case AT_ENTRY: at_entry = val; break;
        case AT_PAGESZ: at_pagesz = val; break;
        }
        av += 2;
    }
    (void)at_pagesz;

    /* 3. Self-relocate */
    self_relocate(at_base);

    /* 4. Walk main binary PT_DYNAMIC to find DT_NEEDED */
    if (!at_phdr || !at_phnum) {
        /* No dynamic info — jump directly */
        goto jump;
    }

    {
        Elf64_Phdr *phdrs = (Elf64_Phdr *)at_phdr;
        uint64_t dyn_vaddr = 0;
        for (uint64_t i = 0; i < at_phnum; i++) {
            if (phdrs[i].p_type == PT_DYNAMIC) {
                dyn_vaddr = phdrs[i].p_vaddr;
                break;
            }
        }

        if (dyn_vaddr) {
            Elf64_Dyn *dyn = (Elf64_Dyn *)dyn_vaddr;
            uint64_t strtab = 0;
            /* First pass: get strtab */
            for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
                if (d->d_tag == DT_STRTAB) { strtab = d->d_val; break; }
            }
            /* Second pass: load DT_NEEDED */
            if (strtab) {
                for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
                    if (d->d_tag == DT_NEEDED) {
                        const char *libname = (const char *)(strtab + d->d_val);
                        /* Try /lib/<name> */
                        char path[128];
                        size_t nlen = ldso_strlen(libname);
                        if (nlen + 6 < sizeof(path)) {
                            ldso_memcpy(path, "/lib/", 5);
                            ldso_memcpy(path + 5, libname, nlen + 1);
                            load_library(path);
                        }
                    }
                }
            }

            /* Apply main binary's own relocations */
            uint64_t rela_off = 0, rela_sz = 0, jmprel = 0, jmpsz = 0;
            uint64_t symtab = 0;
            for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
                switch ((int)d->d_tag) {
                case DT_SYMTAB:   symtab  = d->d_val; break;
                case DT_RELA:     rela_off = d->d_val; break;
                case DT_RELASZ:   rela_sz  = d->d_val; break;
                case DT_JMPREL:   jmprel  = d->d_val; break;
                case DT_PLTRELSZ: jmpsz   = d->d_val; break;
                }
            }
            apply_rela(0, rela_off, rela_sz, symtab, strtab);
            apply_rela(0, jmprel,   jmpsz,   symtab, strtab);
        }
    }

jump:
    /* 5. Jump to main binary entry */
    if (!at_entry) ldso_die("no AT_ENTRY");

    /* Transfer control: restore sp, jump to entry */
    __asm__ volatile(
        "mov %0, %%rsp\n"
        "jmp *%1\n"
        :
        : "r"(sp), "r"(at_entry)
        : "memory"
    );
    for (;;);
}

/* ---- Assembly entry point ---- */
__asm__(
    ".section .text\n"
    ".global _ldso_start\n"
    "_ldso_start:\n"
    "    mov %rsp, %rdi\n"    /* pass sp as first arg */
    "    call ldso_main\n"
    "    ud2\n"
);
