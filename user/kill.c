#include "syscall.h"

static int my_atoi(const char *s) {
    int n = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}
static int my_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void my_write(int fd, const char *s) { sys_write(fd, s, (uint64_t)my_strlen(s)); }

int _start(int argc, char **argv) {
    if (argc < 2) { my_write(1, "Usage: kill [-signum] <pid>\n"); sys_exit(1); }
    int sig = SIGTERM;
    int pid_idx = 1;
    if (argv[1][0] == '-' && argv[1][1] >= '0' && argv[1][1] <= '9') {
        sig = my_atoi(argv[1] + 1);
        if (sig <= 0) sig = SIGTERM;
        pid_idx = 2;
    }
    if (pid_idx >= argc) { my_write(1, "kill: need pid\n"); sys_exit(1); }
    int32_t pid = (int32_t)my_atoi(argv[pid_idx]);
    sys_kill((uint32_t)pid, sig);
    sys_exit(0);
    return 0;
}
