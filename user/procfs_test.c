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
    print_str("procfs_test: my PID=");
    print_num(sys_getpid());
    print_str("\n");

    /* Open /proc/self/status */
    int64_t fd = sys_open("/proc/self/status", 0);
    if (fd < 0) {
        print_str("procfs_test: FAIL - cannot open /proc/self/status\n");
        sys_exit(1);
    }

    print_str("procfs_test: opened /proc/self/status fd=");
    print_num((uint64_t)fd);
    print_str("\n");

    char buf[256];
    int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        print_str("procfs_test: contents:\n");
        sys_write(1, buf, (uint64_t)n);
        print_str("procfs_test: PASS - read /proc/self/status successfully\n");
    } else {
        print_str("procfs_test: FAIL - read returned ");
        print_num((uint64_t)n);
        print_str("\n");
    }

    sys_close((int)fd);
    sys_exit(0);
}
