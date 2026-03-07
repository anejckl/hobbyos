#include "syscall.h"

static void print_str(const char *s) {
    uint64_t len = 0;
    while (s[len]) len++;
    sys_write(1, s, len);
}

static void print_num(uint64_t n) {
    char buf[20];
    int i = 0;
    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    sys_write(1, buf, (uint64_t)i);
}

void _start(void) {
    int fds[2];
    if (sys_pipe(fds) < 0) {
        print_str("pipe_test: FAIL - pipe() failed\n");
        sys_exit(1);
    }

    print_str("pipe_test: created pipe read=");
    print_num((uint64_t)fds[0]);
    print_str(" write=");
    print_num((uint64_t)fds[1]);
    print_str("\n");

    int64_t pid = sys_fork();
    if (pid < 0) {
        print_str("pipe_test: FAIL - fork() failed\n");
        sys_exit(1);
    }

    if (pid == 0) {
        /* Child: close write end, read from pipe */
        sys_close(fds[1]);

        char buf[32];
        int64_t n = sys_read(fds[0], buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            print_str("pipe_test: child read '");
            sys_write(1, buf, (uint64_t)n);
            print_str("'\n");

            /* Check content */
            if (buf[0] == 'H' && buf[1] == 'e' && buf[2] == 'l' &&
                buf[3] == 'l' && buf[4] == 'o') {
                print_str("pipe_test: PASS - pipe communication works!\n");
            } else {
                print_str("pipe_test: FAIL - unexpected data\n");
            }
        } else {
            print_str("pipe_test: FAIL - read returned ");
            print_num((uint64_t)n);
            print_str("\n");
        }

        sys_close(fds[0]);
        sys_exit(0);
    } else {
        /* Parent: close read end, write to pipe */
        sys_close(fds[0]);

        const char *msg = "Hello";
        sys_write(fds[1], msg, 5);
        print_str("pipe_test: parent wrote 'Hello'\n");

        sys_close(fds[1]);

        int32_t status = 0;
        sys_wait(&status);
    }

    sys_exit(0);
}
