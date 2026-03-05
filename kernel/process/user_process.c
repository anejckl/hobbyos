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
 * Switches CR3, copies ELF segment data to user pages, enters user mode. */
static void user_trampoline(void) {
    struct process *proc = scheduler_get_current();

    /* Switch to user address space */
    write_cr3(proc->cr3);

    /* Copy ELF PT_LOAD segments into the already-mapped user pages */
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)proc->user_program_data;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = (const Elf64_Phdr *)
            (proc->user_program_data + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD)
            continue;

        if (phdr->p_filesz > 0) {
            memcpy((void *)phdr->p_vaddr,
                   proc->user_program_data + phdr->p_offset,
                   phdr->p_filesz);
        }
    }

    debug_printf("user_process: PID=%u entering user mode at 0x%x\n",
                 (uint64_t)proc->pid, ehdr->e_entry);

    /* Enter ring 3 */
    enter_usermode(ehdr->e_entry, USER_STACK_TOP,
                   GDT_USER_CODE_RPL3, GDT_USER_DATA_RPL3);
}

int user_process_create(const char *name, const uint8_t *data, uint64_t size) {
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

    /* 3. Load ELF segments (allocate and map pages) */
    if (elf_load(pml4_phys, data, size, &elf_result) < 0) {
        debug_printf("user_process: ELF load failed for '%s'\n", name);
        return -1;
    }

    /* 4. Allocate and map user stack page */
    uint64_t stack_phys = pmm_alloc_page();
    if (!stack_phys) {
        debug_printf("user_process: failed to alloc stack page\n");
        return -1;
    }
    memset(PHYS_TO_VIRT(stack_phys), 0, PAGE_SIZE);
    if (user_vm_map_page(pml4_phys, USER_STACK_BOTTOM,
                         stack_phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER) < 0) {
        debug_printf("user_process: failed to map stack page\n");
        return -1;
    }

    /* 5. Create kernel process with trampoline entry */
    struct process *proc = process_create(name, user_trampoline);
    if (!proc) {
        debug_printf("user_process: failed to create process\n");
        return -1;
    }

    /* 6. Store user-mode info in PCB */
    proc->cr3 = pml4_phys;
    proc->is_user = true;
    proc->user_program_data = data;
    proc->user_program_size = size;

    /* 7. Add to scheduler */
    scheduler_add(proc);

    debug_printf("user_process: created '%s' PID=%u cr3=0x%x entry=0x%x\n",
                 name, (uint64_t)proc->pid, pml4_phys, elf_result.entry_point);
    return 0;
}
