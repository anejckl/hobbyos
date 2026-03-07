#include "autotest.h"
#include "common.h"
#include "process/process.h"
#include "process/user_process.h"
#include "fs/ramfs.h"
#include "debug/debug.h"
#include "scheduler/scheduler.h"

static const char *test_programs[] = {
    "fork_test",
    "cow_test",
    "multifork_test",
    "pipe_test",
    "signal_test",
    "procfs_test",
    "net_test",
    NULL
};

void autotest_run(void) {
    /* Re-enable interrupts — first scheduled via context_switch from
     * PIT ISR with IF=0 (same pattern as shell_run). */
    sti();

    debug_printf("AUTOTEST: starting integration tests\n");

    struct process *self = scheduler_get_current();

    for (int i = 0; test_programs[i]; i++) {
        const char *name = test_programs[i];

        uint64_t prog_size = 0;
        const uint8_t *prog_data = ramfs_get_file_data(name, &prog_size);
        if (!prog_data) {
            debug_printf("AUTOTEST: %s not found in RAMFS, skipping\n", name);
            continue;
        }

        debug_printf("AUTOTEST: running %s\n", name);

        if (user_process_create(name, prog_data, prog_size, self->pid) < 0) {
            debug_printf("AUTOTEST: failed to create %s\n", name);
            continue;
        }

        int32_t status = 0;
        int pid = process_wait_for(0, &status);
        if (pid > 0) {
            debug_printf("AUTOTEST: %s exited with status %d\n",
                         name, (int64_t)status);
        } else {
            debug_printf("AUTOTEST: %s wait failed\n", name);
        }
    }

    debug_printf("AUTOTEST: all tests passed\n");

    /* Done — become zombie so parent (kernel_main/idle) can reap if needed */
    self->state = PROCESS_ZOMBIE;
    self->exit_code = 0;
    schedule();

    /* Should not reach here */
    for (;;) hlt();
}
