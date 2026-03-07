#include "signal.h"
#include "../arch/x86_64/isr.h"
#include "../process/process.h"
#include "../scheduler/scheduler.h"
#include "../debug/debug.h"

void signal_send(uint32_t pid, int sig) {
    if (sig < 1 || sig >= 32)
        return;

    struct process *target = process_get_by_pid(pid);
    if (!target || target->state == PROCESS_UNUSED ||
        target->state == PROCESS_TERMINATED)
        return;

    /* Set pending bit */
    target->sig_pending |= (1u << sig);

    debug_printf("signal: sent signal %d to PID %u\n",
                 (int64_t)sig, (uint64_t)pid);

    /* If target is blocked, wake it so it can handle the signal */
    if (target->state == PROCESS_BLOCKED) {
        target->state = PROCESS_READY;
        scheduler_add(target);
    }
}

void signal_check(struct process *proc, struct interrupt_frame *frame) {
    if (!proc || !proc->is_user || proc->in_signal_handler)
        return;
    if (proc->sig_pending == 0)
        return;

    /* Find lowest pending signal */
    for (int sig = 1; sig < 32; sig++) {
        if (!(proc->sig_pending & (1u << sig)))
            continue;

        /* Clear pending bit */
        proc->sig_pending &= ~(1u << sig);

        uint64_t handler = proc->sig_handlers[sig];

        if (handler == SIG_IGN) {
            /* Ignored */
            continue;
        }

        if (handler == SIG_DFL) {
            /* Default action */
            switch (sig) {
            case SIGCHLD:
                /* Ignore by default */
                continue;
            case SIGKILL:
            case SIGTERM:
            case SIGINT:
            case SIGPIPE:
                /* Terminate process */
                debug_printf("signal: PID %u killed by signal %d\n",
                             (uint64_t)proc->pid, (int64_t)sig);
                proc->state = PROCESS_ZOMBIE;
                proc->exit_code = 128 + sig;

                /* Wake parent if waiting */
                {
                    struct process *parent = process_get_by_pid(proc->ppid);
                    if (parent && parent->state == PROCESS_BLOCKED &&
                        (parent->wait_for_pid == 0 ||
                         parent->wait_for_pid == proc->pid)) {
                        parent->state = PROCESS_READY;
                        scheduler_add(parent);
                    }
                }
                schedule();
                return;
            default:
                /* Unknown signal with default action: terminate */
                proc->state = PROCESS_ZOMBIE;
                proc->exit_code = 128 + sig;
                schedule();
                return;
            }
        }

        /* User-defined handler: redirect execution */
        /* Save current user context */
        proc->sig_saved_rip = frame->rip;
        proc->sig_saved_rsp = frame->rsp;
        proc->sig_saved_rflags = frame->rflags;
        proc->sig_saved_rax = frame->rax;
        proc->sig_saved_rdi = frame->rdi;
        proc->sig_saved_rsi = frame->rsi;
        proc->sig_saved_rdx = frame->rdx;
        proc->in_signal_handler = true;

        /* Set up user stack for signal handler:
         * Push a fake return address that will cause a fault or
         * the handler should call sys_sigreturn() at the end.
         * We set RIP to handler, RDI to signal number. */
        frame->rip = handler;
        frame->rdi = (uint64_t)sig;

        /* Push return address on user stack (the user handler
         * should call sys_sigreturn when done). We push 0 as
         * the return address — if the handler does a bare 'ret'
         * it will fault, which is fine. Proper handlers call
         * sys_sigreturn(). */
        frame->rsp -= 8;
        /* We can't safely write to user stack from here without
         * switching CR3, so we rely on the handler calling
         * sys_sigreturn() explicitly. */

        debug_printf("signal: delivering signal %d to PID %u handler=0x%x\n",
                     (int64_t)sig, (uint64_t)proc->pid, handler);

        /* Only deliver one signal per return-to-user */
        return;
    }
}
