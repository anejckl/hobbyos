#ifndef PROCESS_H
#define PROCESS_H

#include "../common.h"

#define MAX_PROCESSES   64
#define PROCESS_STACK_SIZE 8192   /* 8KB per process kernel stack */

/* Process states */
enum process_state {
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
};

/* CPU context saved during context switch (must match context_switch.asm) */
struct context {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t rip;
};

/* Process Control Block */
struct process {
    uint32_t pid;
    char name[32];
    enum process_state state;
    struct context context;
    uint64_t kernel_stack;       /* Top of kernel stack */
    struct process *next;        /* For scheduler queue */
    uint64_t cr3;                /* Physical address of PML4, 0 = kernel CR3 */
    bool is_user;                /* true if ring 3 process */
    const uint8_t *user_program_data;  /* pointer to embedded program binary */
    uint64_t user_program_size;
};

void process_init(void);
struct process *process_create(const char *name, void (*entry)(void));
struct process *process_get_by_pid(uint32_t pid);
struct process *process_table_get(void);
uint32_t process_get_count(void);

#endif /* PROCESS_H */
