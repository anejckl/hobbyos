#include "syscall.h"

static volatile int sig_received = 0;

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

static void sigint_handler(int sig) {
    print_str("signal_test: handler called for signal ");
    print_num((uint64_t)sig);
    print_str("\n");
    sig_received = 1;
    sys_sigreturn();
}

void _start(void) {
    print_str("signal_test: starting\n");

    int64_t pid = sys_fork();

    if (pid == 0) {
        /* Child: install SIGINT handler and wait */
        sys_sigaction(SIGINT, (uint64_t)sigint_handler);
        print_str("signal_test: child installed SIGINT handler\n");

        /* Busy-wait for signal (crude but works without sleep) */
        for (volatile uint64_t i = 0; i < 50000000; i++) {
            if (sig_received)
                break;
        }

        if (sig_received) {
            print_str("signal_test: PASS - signal received!\n");
        } else {
            print_str("signal_test: FAIL - signal not received\n");
        }

        sys_exit(0);
    } else if (pid > 0) {
        /* Parent: wait a bit then send SIGINT to child */
        /* Crude delay */
        for (volatile uint64_t i = 0; i < 10000000; i++)
            ;

        print_str("signal_test: parent sending SIGINT to child ");
        print_num((uint64_t)pid);
        print_str("\n");
        sys_kill((uint32_t)pid, SIGINT);

        int32_t status = 0;
        sys_wait(&status);
        print_str("signal_test: child exited\n");
    } else {
        print_str("signal_test: fork failed!\n");
    }

    sys_exit(0);
}
