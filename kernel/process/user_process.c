#include "user_process.h"
#include "process.h"
#include "../memory/user_vm.h"
#include "../memory/pmm.h"
#include "../memory/vmm.h"
#include "../scheduler/scheduler.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/usermode.h"
#include "../elf/elf.h"
#include "../elf/elf_loader.h"
#include "../string.h"
#include "../debug/debug.h"

/* Trampoline: runs in kernel mode on the process's kernel stack.
 * Switches CR3 and enters user mode. ELF data was already copied by elf_load(). */
static void user_trampoline(void) {
    struct process *proc = scheduler_get_current();
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)proc->user_program_data;

    write_cr3(proc->cr3);

    debug_printf("user_process: PID=%u entering user mode at 0x%x rsp=0x%x\n",
                 (uint64_t)proc->pid, ehdr->e_entry, proc->user_entry_rsp);

    /* Enter ring 3 with argc in RDI, argv ptr in RSI */
    enter_usermode_args(ehdr->e_entry, proc->user_entry_rsp,
                        GDT_USER_CODE_RPL3, GDT_USER_DATA_RPL3,
                        (uint64_t)proc->user_entry_argc,
                        proc->user_entry_argv);
}

int user_process_create_args(const char *name, const uint8_t *data, uint64_t size,
                             uint32_t ppid, const char *args);

int user_process_create(const char *name, const uint8_t *data, uint64_t size, uint32_t ppid) {
    return user_process_create_args(name, data, size, ppid, NULL);
}

int user_process_create_args(const char *name, const uint8_t *data, uint64_t size,
                             uint32_t ppid, const char *args) {
#define UPCA_MAX_ARGS 32
    /* 1. Validate ELF */
    struct elf_load_result elf_result;
    if (elf_validate(data, size) < 0) {
        debug_printf("user_process: invalid ELF for '%s'\n", name);
        return -1;
    }

    /* 2. Create address space */
    uint64_t pml4_phys = user_vm_create_address_space();
    if (!pml4_phys) {
        debug_printf("user_process: failed to create address space\n");
        return -1;
    }

    /* 3. Load ELF segments (allocate, copy, and map pages) */
    if (elf_load(pml4_phys, data, size, 0, &elf_result) < 0) {
        debug_printf("user_process: ELF load failed for '%s'\n", name);
        return -1;
    }

    /* 4. Allocate user stack page */
    uint64_t stack_phys = pmm_alloc_page();
    if (!stack_phys) {
        debug_printf("user_process: failed to alloc stack page\n");
        return -1;
    }
    memset(PHYS_TO_VIRT(stack_phys), 0, PAGE_SIZE);

    /* 5. Parse args string into argc/argv */
    const char *kern_argv[UPCA_MAX_ARGS];
    int argc = 0;
    char args_buf[512];

    kern_argv[argc++] = name;  /* argv[0] = program name */

    if (args && args[0]) {
        size_t args_len = strlen(args);
        if (args_len >= sizeof(args_buf))
            args_len = sizeof(args_buf) - 1;
        memcpy(args_buf, args, args_len);
        args_buf[args_len] = '\0';

        char *tok = args_buf;
        while (*tok && argc < UPCA_MAX_ARGS) {
            while (*tok == ' ') tok++;
            if (!*tok) break;
            kern_argv[argc++] = tok;
            while (*tok && *tok != ' ') tok++;
            if (*tok == ' ') { *tok = '\0'; tok++; }
        }
    }

    /* 6. Set up SysV stack layout (writes strings + pointers via PHYS_TO_VIRT) */
    uint64_t entry_rsp, entry_argv_ptr;
    if (elf_setup_stack(stack_phys, USER_STACK_BOTTOM, argc, kern_argv,
                        &elf_result, &entry_rsp, &entry_argv_ptr) < 0) {
        debug_printf("user_process: elf_setup_stack failed for '%s'\n", name);
        return -1;
    }

    /* 7. Map the stack page */
    if (user_vm_map_page(pml4_phys, USER_STACK_BOTTOM,
                         stack_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER) < 0) {
        debug_printf("user_process: failed to map stack page\n");
        return -1;
    }

    /* 7b. Map legacy argv page at USER_ARGV_ADDR for programs using get_argv() */
    if (args && args[0]) {
        uint64_t argv_phys = pmm_alloc_page();
        if (argv_phys) {
            char *argv_page = (char *)PHYS_TO_VIRT(argv_phys);
            size_t args_len = strlen(args);
            if (args_len >= PAGE_SIZE)
                args_len = PAGE_SIZE - 1;
            memcpy(argv_page, args, args_len);
            argv_page[args_len] = '\0';
            user_vm_map_page(pml4_phys, USER_ARGV_ADDR,
                             argv_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
        }
    }

    /* 8. Create kernel process with trampoline entry */
    struct process *proc = process_create(name, user_trampoline);
    if (!proc) {
        debug_printf("user_process: failed to create process\n");
        return -1;
    }

    /* 9. Store user-mode info in PCB */
    proc->cr3 = pml4_phys;
    proc->is_user = true;
    proc->user_program_data = data;
    proc->user_program_size = size;
    proc->ppid = ppid;
    proc->user_entry_rsp  = entry_rsp;
    proc->user_entry_argv = entry_argv_ptr;
    proc->user_entry_argc = argc;

    /* Initialize mmap/brk/epoll fields */
    proc->mmap_next    = MMAP_BASE;
    proc->brk_start    = elf_result.load_end;
    proc->brk_current  = elf_result.load_end;
    proc->epoll_fd_idx = -1;

    /* 10. Add to scheduler */
    scheduler_add(proc);

    debug_printf("user_process: created '%s' PID=%u cr3=0x%x entry=0x%x argc=%d\n",
                 name, (uint64_t)proc->pid, pml4_phys,
                 elf_result.entry_point, (uint64_t)argc);
    return 0;
}
