#include "syscall.h"
#include "ulib.h"

void _start(void) {
    print("waitpid_test: starting\n");

    /* Fork 3 children, each exits with a different status */
    int32_t child_pids[3];
    int exit_codes[3] = {10, 20, 30};

    for (int i = 0; i < 3; i++) {
        int64_t pid = sys_fork();
        if (pid == 0) {
            /* Child: exit with unique status */
            sys_exit(exit_codes[i]);
        }
        child_pids[i] = (int32_t)pid;
    }

    /* Wait for middle child specifically using waitpid */
    int32_t status = -1;
    int64_t reaped = sys_waitpid(child_pids[1], &status, 0);

    if (reaped != (int64_t)child_pids[1] || status != 20) {
        print("waitpid_test: FAIL - expected PID=");
        print_num((uint64_t)child_pids[1]);
        print(" status=20, got PID=");
        print_num((uint64_t)reaped);
        print(" status=");
        print_num((uint64_t)status);
        print("\n");
        sys_exit(1);
    }

    print("waitpid_test: waited for middle child OK\n");

    /* Wait for remaining children with waitpid(-1) */
    int reaped_count = 0;
    for (int i = 0; i < 2; i++) {
        status = -1;
        reaped = sys_waitpid(-1, &status, 0);
        if (reaped > 0) {
            reaped_count++;
        }
    }

    if (reaped_count == 2) {
        print("waitpid_test: PASS\n");
    } else {
        print("waitpid_test: FAIL - didn't reap all children\n");
    }

    sys_exit(0);
}
