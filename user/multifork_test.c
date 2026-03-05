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
    print_str("multifork: parent PID=");
    print_num(sys_getpid());
    print_str("\n");

    /* Fork 3 children, each exits with different status */
    for (int c = 0; c < 3; c++) {
        int64_t pid = sys_fork();
        if (pid == 0) {
            /* Child */
            print_str("multifork: child PID=");
            print_num(sys_getpid());
            print_str(" exiting with status ");
            print_num((uint64_t)(c + 10));
            print_str("\n");
            sys_exit(c + 10);
        }
    }

    /* Parent: wait for all 3 children */
    for (int c = 0; c < 3; c++) {
        int32_t status = 0;
        int64_t reaped = sys_wait(&status);
        print_str("multifork: reaped PID=");
        print_num((uint64_t)reaped);
        print_str(" status=");
        print_num((uint64_t)status);
        print_str("\n");
    }

    print_str("multifork: all children reaped, done.\n");
    sys_exit(0);
}
