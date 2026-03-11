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
        } else {
            filename = argv[i];
        }
    }

    if (!filename) { my_write(1, "tail: need a file argument\n"); sys_exit(1); }
    int64_t fd = sys_open(filename, 0);
    if (fd < 0) { my_write(1, "tail: cannot open file\n"); sys_exit(1); }

    /* Read up to 64KB into buffer */
    static char buf[65536];
    int64_t total = sys_read((int)fd, buf, sizeof(buf) - 1);
    sys_close((int)fd);
    if (total <= 0) sys_exit(0);
    buf[total] = '\0';

    /* Find start of last n_lines */
    int lines_found = 0;
    int start = (int)total;
    while (start > 0) {
        start--;
        if (buf[start] == '\n') {
            lines_found++;
            if (lines_found == n_lines) { start++; break; }
        }
    }
    if (lines_found < n_lines) start = 0;

    sys_write(1, buf + start, (uint64_t)(total - start));
    sys_exit(0);
    return 0;
}
