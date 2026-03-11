#include "syscall.h"

static int my_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void my_write(int fd, const char *s) { sys_write(fd, s, (uint64_t)my_strlen(s)); }

int _start(int argc, char **argv) {
    if (argc < 3) { my_write(1, "Usage: cp <src> <dst>\n"); sys_exit(1); }

    int64_t src_fd = sys_open(argv[1], 0);
    if (src_fd < 0) { my_write(1, "cp: cannot open source\n"); sys_exit(1); }

    int64_t dst_fd = sys_open(argv[2], O_CREAT | 2 /* write */);
    if (dst_fd < 0) { sys_close((int)src_fd); my_write(1, "cp: cannot open dest\n"); sys_exit(1); }

    char buf[512];
    int64_t n;
    while ((n = sys_read((int)src_fd, buf, sizeof(buf))) > 0) {
        sys_write((int)dst_fd, buf, (uint64_t)n);
    }
    sys_close((int)src_fd);
    sys_close((int)dst_fd);
    sys_exit(0);
    return 0;
}
