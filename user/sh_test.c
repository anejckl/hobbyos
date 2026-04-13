#include "syscall.h"

static int my_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void my_write(const char *s) { sys_write(1, s, (uint64_t)my_strlen(s)); }

static int my_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 1;
        if (!a[i]) return 0;
    }
    return 0;
}

/* Test 1: pipe + fork + dup2 — parent writes, child reads */
static int test_pipe_basic(void) {
    int fds[2];
    if (sys_pipe(fds) < 0) { my_write("FAIL: pipe creation\n"); return 1; }

    int64_t pid = sys_fork();
    if (pid == 0) {
        /* Child: read from pipe */
        sys_close(fds[1]);
        char buf[64];
        int64_t n = sys_read(fds[0], buf, sizeof(buf) - 1);
        sys_close(fds[0]);
        if (n > 0) {
            buf[n] = '\0';
            if (my_strncmp(buf, "hello pipe", 10) == 0) {
                sys_exit(0);
            }
        }
        sys_exit(1);
    } else {
        /* Parent: write to pipe */
        sys_close(fds[0]);
        sys_write(fds[1], "hello pipe", 10);
        sys_close(fds[1]);
        int32_t status = 0;
        sys_waitpid((int32_t)pid, &status, 0);
        if (status != 0) {
            my_write("sh_test: FAIL pipe basic\n");
            return 1;
        }
        my_write("sh_test: PASS pipe basic\n");
    }
    return 0;
}

/* Test 2: pipe + fork + dup2 + exec (echo piped to grep) */
static int test_pipe_exec(void) {
    int fds[2];
    if (sys_pipe(fds) < 0) { my_write("FAIL: pipe creation\n"); return 1; }

    /* Fork writer: exec echo */
    int64_t writer = sys_fork();
    if (writer == 0) {
        sys_close(fds[0]);
        sys_dup2(fds[1], 1);
        sys_close(fds[1]);
        char *argv[] = {"echo", "PipeExecTest", NULL};
        sys_execv("/bin/echo", 2, argv);
        sys_exit(127);
    }

    /* Fork reader: exec grep */
    int64_t reader = sys_fork();
    if (reader == 0) {
        sys_close(fds[1]);
        sys_dup2(fds[0], 0);
        sys_close(fds[0]);
        char *argv[] = {"grep", "PipeExec", NULL};
        sys_execv("/bin/grep", 2, argv);
        sys_exit(127);
    }

    /* Parent: close pipe, wait for both */
    sys_close(fds[0]);
    sys_close(fds[1]);
    int32_t s1 = 0, s2 = 0;
    sys_waitpid((int32_t)writer, &s1, 0);
    sys_waitpid((int32_t)reader, &s2, 0);

    if (s1 == 0 && s2 == 0) {
        my_write("sh_test: PASS pipe exec\n");
        return 0;
    }
    my_write("sh_test: FAIL pipe exec\n");
    return 1;
}

int _start(void) {
    my_write("sh_test: starting\n");
    int failures = 0;
    failures += test_pipe_basic();
    failures += test_pipe_exec();
    if (failures == 0) {
        my_write("sh_test: PASS - all tests passed\n");
    } else {
        my_write("sh_test: FAIL\n");
    }
    sys_exit(failures);
    return 0;
}
