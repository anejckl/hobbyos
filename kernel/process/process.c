#include "process.h"
#include "../memory/kheap.h"
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

    /* Allocate kernel stack */
    void *stack = kmalloc(PROCESS_STACK_SIZE);
    if (!stack)
        return NULL;

    /* Initialize PCB */
    proc->pid = next_pid++;
    strncpy(proc->name, name, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    proc->state = PROCESS_READY;
    proc->kernel_stack = (uint64_t)stack + PROCESS_STACK_SIZE;
    proc->next = NULL;

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
