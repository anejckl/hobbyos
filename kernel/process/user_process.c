#include "user_process.h"
#include "process.h"
#include "../memory/user_vm.h"
#include "../memory/pmm.h"
#include "../memory/vmm.h"
#include "../scheduler/scheduler.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/usermode.h"
#include "../string.h"
#include "../debug/debug.h"

/* Trampoline: runs in kernel mode on the process's kernel stack.
 * Switches CR3, copies program binary to user pages, enters user mode. */
static void user_trampoline(void) {
    struct process *proc = scheduler_get_current();

    /* Switch to user address space */
    write_cr3(proc->cr3);

    /* Copy program binary to user code area */
    memcpy((void *)USER_CODE_BASE, proc->user_program_data, proc->user_program_size);

    debug_printf("user_process: PID=%u entering user mode\n", (uint64_t)proc->pid);

    /* Enter ring 3 */
    enter_usermode(USER_CODE_BASE, USER_STACK_TOP,
                   GDT_USER_CODE_RPL3, GDT_USER_DATA_RPL3);
}

int user_process_create(const char *name, const uint8_t *data, uint64_t size) {
    /* 1. Create address space */
    uint64_t pml4_phys = user_vm_create_address_space();
    if (!pml4_phys) {
        debug_printf("user_process: failed to create address space\n");
        return -1;
    }

    /* 2. Allocate and map user code pages at USER_CODE_BASE */
    uint64_t num_code_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (num_code_pages == 0)
        num_code_pages = 1;

    for (uint64_t i = 0; i < num_code_pages; i++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            debug_printf("user_process: failed to alloc code page\n");
            return -1;
        }
        /* Zero the page */
        memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        /* Map in user address space */
        if (user_vm_map_page(pml4_phys, USER_CODE_BASE + i * PAGE_SIZE,
                             phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER) < 0) {
            debug_printf("user_process: failed to map code page\n");
            return -1;
        }
    }

    /* 3. Allocate and map user stack page */
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

    /* 4. Create kernel process with trampoline entry */
    struct process *proc = process_create(name, user_trampoline);
    if (!proc) {
        debug_printf("user_process: failed to create process\n");
        return -1;
    }

    /* 5. Store user-mode info in PCB */
    proc->cr3 = pml4_phys;
    proc->is_user = true;
    proc->user_program_data = data;
    proc->user_program_size = size;

    /* 6. Add to scheduler */
    scheduler_add(proc);

    debug_printf("user_process: created '%s' PID=%u cr3=0x%x size=%u\n",
                 name, (uint64_t)proc->pid, pml4_phys, size);
    return 0;
}
