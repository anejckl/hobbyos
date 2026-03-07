#include "scheduler.h"
#include "../process/process.h"
#include "../arch/x86_64/tss.h"
#include "../debug/debug.h"
#include "../net/net.h"

static struct process *current_process = NULL;
static struct process *ready_queue_head = NULL;
static struct process *ready_queue_tail = NULL;
static volatile uint64_t tick_count = 0;
static bool scheduler_enabled = false;
static uint64_t kernel_cr3 = 0;

/* Idle process: just halts waiting for interrupts */
static void idle_task(void) {
    for (;;) {
        hlt();
    }
}

void scheduler_init(void) {
    /* Save boot CR3 for kernel processes */
    kernel_cr3 = read_cr3();

    /* Create idle process */
    struct process *idle = process_create("idle", idle_task);
    if (idle) {
        idle->state = PROCESS_RUNNING;
        current_process = idle;
    }

    scheduler_enabled = true;
    debug_printf("Scheduler initialized, idle process PID=%u\n",
                 (uint64_t)idle->pid);
}

void scheduler_add(struct process *proc) {
    proc->state = PROCESS_READY;
    proc->next = NULL;

    if (!ready_queue_head) {
        ready_queue_head = proc;
        ready_queue_tail = proc;
    } else {
        ready_queue_tail->next = proc;
        ready_queue_tail = proc;
    }
}

/* Called from PIT interrupt handler */
void scheduler_tick(void) {
    tick_count++;
    if (!scheduler_enabled)
        return;

    /* Network timer (every tick = 10ms at 100Hz) */
    net_tick();

    /* Schedule every 10 ticks (~100ms at 100Hz) */
    if (tick_count % 10 == 0)
        schedule();
}

void schedule(void) {
    if (!ready_queue_head)
        return;

    struct process *next = ready_queue_head;
    ready_queue_head = next->next;
    if (!ready_queue_head)
        ready_queue_tail = NULL;

    if (current_process && current_process->state == PROCESS_RUNNING) {
        current_process->state = PROCESS_READY;
        scheduler_add(current_process);
    }

    struct process *old = current_process;
    current_process = next;
    next->state = PROCESS_RUNNING;

    /* Update TSS RSP0 for the new process */
    tss_set_rsp0(next->kernel_stack);

    /* Switch CR3 for per-process address spaces */
    if (next->cr3)
        write_cr3(next->cr3);
    else
        write_cr3(kernel_cr3);

    /* Perform context switch */
    if (old && old != next)
        context_switch(&old->context, &next->context);
}

struct process *scheduler_get_current(void) {
    return current_process;
}
