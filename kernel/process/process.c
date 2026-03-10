#include "process.h"
#include "../memory/kheap.h"
#include "../memory/vmm.h"
#include "../scheduler/scheduler.h"
#include "../string.h"
#include "../debug/debug.h"

static struct process process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
}

struct process *process_create(const char *name, void (*entry)(void)) {
    /* Find a free slot */
    struct process *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_UNUSED) {
            proc = &process_table[i];
            break;
        }
    }
    if (!proc)
        return NULL;

    /* Allocate kernel stack with guard page */
    void *guard_and_stack = kmalloc_page_aligned(PAGE_SIZE + PROCESS_STACK_SIZE);
    if (!guard_and_stack)
        return NULL;
    vmm_unmap_page((uint64_t)guard_and_stack);  /* Guard page */

    /* Initialize PCB */
    proc->pid = next_pid++;
    strncpy(proc->name, name, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    proc->state = PROCESS_READY;
    proc->kernel_stack_guard = (uint64_t)guard_and_stack;
    proc->kernel_stack = (uint64_t)guard_and_stack + PAGE_SIZE + PROCESS_STACK_SIZE;
    proc->next = NULL;
    proc->cr3 = 0;
    proc->is_user = false;
    proc->user_program_data = NULL;
    proc->user_program_size = 0;
    proc->ppid = 0;
    proc->exit_code = 0;
    proc->wait_for_pid = 0;

    /* Initialize FD table with console on 0/1/2 */
    memset(proc->fd_table, 0, sizeof(proc->fd_table));
    for (int i = 0; i < 3; i++)
        proc->fd_table[i].type = FD_CONSOLE;

    /* Initialize signal state */
    proc->sig_pending = 0;
    memset(proc->sig_handlers, 0, sizeof(proc->sig_handlers));
    proc->in_signal_handler = false;

    /* Initialize mmap/brk fields */
    memset(proc->vmas, 0, sizeof(proc->vmas));
    proc->mmap_next   = MMAP_BASE;
    proc->brk_start   = 0;
    proc->brk_current = 0;

    /* Initialize epoll state */
    proc->epoll_fd_idx      = -1;
    proc->epoll_timeout_ticks = 0;
    proc->epoll_events_ptr  = NULL;
    proc->epoll_maxevents   = 0;

    /* Set up initial context so context_switch will "return" to entry */
    memset(&proc->context, 0, sizeof(struct context));
    proc->context.rip = (uint64_t)entry;
    proc->context.rsp = proc->kernel_stack;
    proc->context.rbp = 0;

    debug_printf("Process created: PID=%u name=%s entry=0x%x\n",
                 (uint64_t)proc->pid, name, (uint64_t)entry);

    return proc;
}

struct process *process_get_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROCESS_UNUSED && process_table[i].pid == pid)
            return &process_table[i];
    }
    return NULL;
}

struct process *process_table_get(void) {
    return process_table;
}

uint32_t process_get_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROCESS_UNUSED)
            count++;
    }
    return count;
}

struct process *process_find_zombie_child(uint32_t parent_pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_ZOMBIE &&
            process_table[i].ppid == parent_pid)
            return &process_table[i];
    }
    return NULL;
}

bool process_has_children(uint32_t parent_pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].ppid == parent_pid &&
            process_table[i].state != PROCESS_UNUSED &&
            process_table[i].state != PROCESS_TERMINATED)
            return true;
    }
    return false;
}

struct process *process_alloc(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_UNUSED) {
            memset(&process_table[i], 0, sizeof(struct process));
            process_table[i].pid = next_pid++;
            /* epoll_fd_idx = -1 means not blocked on epoll */
            process_table[i].epoll_fd_idx = -1;
            process_table[i].mmap_next = MMAP_BASE;
            return &process_table[i];
        }
    }
    return NULL;
}

int process_wait_for(uint32_t child_pid, int32_t *status) {
    struct process *cur = scheduler_get_current();

    /* Check for zombie child first */
    struct process *zombie = NULL;
    if (child_pid == 0)
        zombie = process_find_zombie_child(cur->pid);
    else {
        struct process *child = process_get_by_pid(child_pid);
        if (child && child->ppid == cur->pid && child->state == PROCESS_ZOMBIE)
            zombie = child;
    }

    if (zombie) {
        uint32_t zpid = zombie->pid;
        if (status)
            *status = zombie->exit_code;
        zombie->state = PROCESS_UNUSED;
        return (int)zpid;
    }

    /* No zombie yet — do we have any living children? */
    if (!process_has_children(cur->pid))
        return -1;

    /* Block until a child exits */
    cur->wait_for_pid = child_pid;
    cur->state = PROCESS_BLOCKED;
    schedule();

    /* Re-enable interrupts: we were rescheduled from inside a PIT ISR
     * (which runs with IF=0), and context_switch doesn't restore RFLAGS. */
    sti();

    /* Resumed — find and reap the zombie */
    if (child_pid == 0)
        zombie = process_find_zombie_child(cur->pid);
    else {
        struct process *child = process_get_by_pid(child_pid);
        if (child && child->ppid == cur->pid && child->state == PROCESS_ZOMBIE)
            zombie = child;
    }

    if (zombie) {
        uint32_t zpid = zombie->pid;
        if (status)
            *status = zombie->exit_code;
        zombie->state = PROCESS_UNUSED;
        return (int)zpid;
    }

    return -1;
}
