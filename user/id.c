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
    uint64_t gid = sys_getgid();

    /* Get euid/egid: fork trick not needed, we just print uid/gid
     * since euid/egid are returned by the same process context */
    print_str("uid=");
    print_num(uid);
    print_str(" gid=");
    print_num(gid);

    /* Print username for uid */
    if (uid == 0)
        print_str("(root)");

    print_str(" pid=");
    print_num(sys_getpid());
    print_str("\n");

    sys_exit(0);
}
