#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "../common.h"
#include "../process/process.h"

void scheduler_init(void);
void scheduler_add(struct process *proc);
void scheduler_tick(void);
void schedule(void);
struct process *scheduler_get_current(void);

/* Defined in context_switch.asm */
extern void context_switch(struct context *old, struct context *new_ctx);

#endif /* SCHEDULER_H */
