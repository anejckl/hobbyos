#include "syscall.h"
#include "ulib.h"

void _start(void) {
    uint64_t my_pid = sys_getpid();
    print("exec_test: starting, PID=");
    print_num(my_pid);
    print("\n");

    int64_t pid = sys_fork();

    if (pid == 0) {
        /* Child: exec into hello program */
        print("exec_test: child about to exec hello\n");
        int64_t ret = sys_exec("hello");
        /* If exec succeeds, we never reach here */
        print("exec_test: exec failed with ");
        print_num((uint64_t)ret);
        print("\n");
        sys_exit(1);
    } else if (pid > 0) {
        /* Parent: wait for child */
        int32_t status = -1;
        int64_t reaped = sys_wait(&status);

        print("exec_test: child exited with status ");
        print_num((uint64_t)status);
        print(", reaped PID=");
        print_num((uint64_t)reaped);
        print("\n");

        if (status == 0) {
            print("exec_test: PASS - fork/exec/wait works!\n");
        } else {
            print("exec_test: FAIL - unexpected exit status\n");
        }
    } else {
        print("exec_test: FAIL - fork failed!\n");
    }

    sys_exit(0);
}
