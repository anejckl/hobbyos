#include "syscall.h"

static int my_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void my_write(const char *s) { sys_write(1, s, (uint64_t)my_strlen(s)); }

int _start(void) {
    int64_t fd = sys_open("/proc/net/if", 0);
    if (fd < 0) { my_write("ifconfig: /proc/net/if not available\n"); sys_exit(1); }
    char buf[1024];
    int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
    sys_close((int)fd);
    if (n > 0) {
        buf[n] = '\0';
        sys_write(1, buf, (uint64_t)n);
    }
    sys_exit(0);
    return 0;
}
