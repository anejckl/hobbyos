#ifndef SIGNAL_H
#define SIGNAL_H

#include "../common.h"

/* Signal numbers */
#define SIGINT   2
#define SIGFPE   8
#define SIGKILL  9
#define SIGSEGV  11
#define SIGPIPE  13
#define SIGTERM  15
#define SIGCHLD  17
#define SIGTSTP  20

#define SIG_DFL  0
#define SIG_IGN  1

struct process;
struct interrupt_frame;

/* Send a signal to a process by PID.
 * If target is blocked, wake it. */
void signal_send(uint32_t pid, int sig);

/* Check for pending signals before returning to usermode.
 * Called from syscall handler/interrupt return path.
 * May modify frame to redirect execution to signal handler. */
void signal_check(struct process *proc, struct interrupt_frame *frame);

#endif /* SIGNAL_H */
