#ifndef PROCESS_H
#define PROCESS_H

#include "../common.h"

#define MAX_PROCESSES   64
#define PROCESS_STACK_SIZE 16384  /* 16KB per process kernel stack */
#define PROCESS_MAX_FDS 32

/* File descriptor types */
#define FD_NONE         0
#define FD_VFS          1
#define FD_PIPE_READ    2
#define FD_PIPE_WRITE   3
#define FD_CONSOLE      4   /* stdin/stdout/stderr */
#define FD_DEVICE       5   /* device files in /dev */
#define FD_EXT2         6   /* ext2 filesystem files/dirs */
#define FD_SOCKET       7   /* network socket */

struct process_fd {
    int type;           /* FD_NONE, FD_VFS, FD_PIPE_READ, FD_PIPE_WRITE, FD_CONSOLE, FD_DEVICE, FD_EXT2 */
    void *data;         /* vfs_node* for VFS, pipe* for pipes */
    uint64_t offset;    /* file offset for VFS files */
};

/* Process states */
enum process_state {
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE,
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
    uint64_t kernel_stack_guard; /* Guard page address (unmapped, 0 if none) */
    struct process *next;        /* For scheduler queue */
    uint64_t cr3;                /* Physical address of PML4, 0 = kernel CR3 */
    bool is_user;                /* true if ring 3 process */
    const uint8_t *user_program_data;  /* pointer to embedded program binary */
    uint64_t user_program_size;
    uint32_t ppid;               /* Parent PID (0 = kernel/init) */
    int32_t exit_code;           /* From sys_exit() */
    uint32_t wait_for_pid;       /* If BLOCKED on wait: child PID (0 = any) */

    /* Per-process file descriptor table */
    struct process_fd fd_table[PROCESS_MAX_FDS];

    /* First user-mode entry state (set by user_process_create_args / syscall_execv) */
    uint64_t user_entry_rsp;     /* RSP for first entry to user mode */
    uint64_t user_entry_argv;    /* user-space argv[] pointer */
    int      user_entry_argc;    /* argc for first entry */

    /* Signal support */
    uint32_t sig_pending;        /* bitmask of pending signals */
    uint64_t sig_handlers[32];   /* per-signal handler addresses (0=SIG_DFL, 1=SIG_IGN) */
    uint64_t sig_saved_rip;      /* saved RIP before signal delivery */
    uint64_t sig_saved_rsp;      /* saved RSP before signal delivery */
    uint64_t sig_saved_rflags;   /* saved RFLAGS before signal delivery */
    uint64_t sig_saved_rax;      /* saved RAX */
    uint64_t sig_saved_rdi;      /* saved RDI */
    uint64_t sig_saved_rsi;      /* saved RSI */
    uint64_t sig_saved_rdx;      /* saved RDX */
    bool in_signal_handler;      /* true while delivering a signal */
};

void process_init(void);
struct process *process_create(const char *name, void (*entry)(void));
struct process *process_get_by_pid(uint32_t pid);
struct process *process_table_get(void);
uint32_t process_get_count(void);
struct process *process_find_zombie_child(uint32_t parent_pid);
bool process_has_children(uint32_t parent_pid);
struct process *process_alloc(void);
int process_wait_for(uint32_t child_pid, int32_t *status);

#endif /* PROCESS_H */
