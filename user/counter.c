#include "syscall.h"

static void print_str(const char *s) {
    uint64_t len = 0;
    while (s[len]) len++;
    sys_write(1, s, len);
}

static void delay(void) {
    for (volatile uint64_t i = 0; i < 10000000; i++)
        ;
}

void _start(void) {
    for (int i = 1; i <= 5; i++) {
        print_str("Count: ");
        char digit = '0' + i;
        sys_write(1, &digit, 1);
        print_str("\n");
        delay();
    }
    print_str("Counter done.\n");
    sys_exit(0);
}
