#include "syscall.h"

static int my_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }

int _start(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) sys_write(1, " ", 1);
        sys_write(1, argv[i], (uint64_t)my_strlen(argv[i]));
    }
    sys_write(1, "\n", 1);
    sys_exit(0);
    return 0;
}
