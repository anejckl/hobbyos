#include "syscall.h"
#include "ulib.h"

void _start(void) {
    print("fork_exec_test: starting\n");
    int64_t pid = sys_fork();
    if (pid == 0) {
        /* child: exec argv_test with arguments */
        char *argv[] = {"argv_test", "hello", "world", (char *)0};
        int64_t ret = sys_execv("argv_test", 3, argv);
        (void)ret;
        print("fork_exec_test: execv failed\n");
        sys_exit(1);
    } else if (pid > 0) {
        /* parent: wait for child */
        int32_t status = -1;
        sys_wait(&status);
        if (status == 0)
            print("fork_exec_test: PASS\n");
        else
            print("fork_exec_test: FAIL\n");
    } else {
        print("fork_exec_test: fork failed\n");
    }
    sys_exit(0);
}
