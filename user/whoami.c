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
    uint64_t uid = sys_getuid();

    if (uid == 0) {
        print_str("root\n");
    } else {
        print_str("user");
        print_num(uid);
        print_str("\n");
    }

    sys_exit(0);
}
