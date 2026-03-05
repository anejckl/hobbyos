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

/* This variable will be shared via COW, then copied on write */
volatile uint64_t shared_var = 12345;

void _start(void) {
    print_str("cow_test: before fork, shared_var=");
    print_num(shared_var);
    print_str("\n");

    int64_t pid = sys_fork();

    if (pid == 0) {
        /* Child: write to trigger COW */
        shared_var = 99999;
        print_str("cow_test: child wrote shared_var=");
        print_num(shared_var);
        print_str("\n");
        sys_exit(0);
    } else {
        /* Parent: wait for child, then check our copy */
        int32_t status = 0;
        sys_wait(&status);
        print_str("cow_test: parent shared_var=");
        print_num(shared_var);
        print_str(" (should be 12345)\n");

        if (shared_var == 12345) {
            print_str("cow_test: PASS - COW isolation works!\n");
        } else {
            print_str("cow_test: FAIL - COW isolation broken!\n");
        }
    }

    sys_exit(0);
}
