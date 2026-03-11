#include "syscall.h"

static int my_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void my_write(int fd, const char *s) { sys_write(fd, s, (uint64_t)my_strlen(s)); }
static int my_atoi(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n;
}

int _start(int argc, char **argv) {
    int n_lines = 10;
    const char *filename = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'n') {
            if (i + 1 < argc) { n_lines = my_atoi(argv[++i]); }
        } else if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            n_lines = my_atoi(argv[i] + 1);
        } else {
            filename = argv[i];
        }
    }

    int64_t fd = filename ? sys_open(filename, 0) : 0;
    if (fd < 0) { my_write(1, "head: cannot open file\n"); sys_exit(1); }

    char c;
    int lines = 0;
    while (lines < n_lines && sys_read((int)fd, &c, 1) > 0) {
        sys_write(1, &c, 1);
        if (c == '\n') lines++;
    }

    if (filename) sys_close((int)fd);
    sys_exit(0);
    return 0;
}
