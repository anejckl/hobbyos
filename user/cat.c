#include "syscall.h"

int _start(int argc, char **argv) {
    int64_t fd = (argc >= 2) ? sys_open(argv[1], 0) : 0;
    if (fd < 0) {
        sys_write(1, "cat: cannot open file\n", 22);
        sys_exit(1);
    }
    char buf[512];
    int64_t n;
    while ((n = sys_read((int)fd, buf, sizeof(buf))) > 0)
        sys_write(1, buf, (uint64_t)n);
    if (argc >= 2) sys_close((int)fd);
    sys_exit(0);
    return 0;
}
