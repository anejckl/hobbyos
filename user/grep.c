#include "syscall.h"

static int my_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void my_write(int fd, const char *s) { sys_write(fd, s, (uint64_t)my_strlen(s)); }

static int my_strstr(const char *hay, const char *ndl) {
    int nl = my_strlen(ndl);
    if (nl == 0) return 1;
    for (int i = 0; hay[i]; i++) {
        int match = 1;
        for (int j = 0; j < nl; j++) {
            if (!hay[i+j] || hay[i+j] != ndl[j]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

int _start(int argc, char **argv) {
    if (argc < 3) { my_write(1, "Usage: grep <pattern> <file>\n"); sys_exit(1); }
    const char *pattern = argv[1];
    int64_t fd = sys_open(argv[2], 0);
    if (fd < 0) { my_write(1, "grep: cannot open file\n"); sys_exit(1); }
    char line[1024];
    int llen = 0;
    char c;
    while (sys_read((int)fd, &c, 1) > 0) {
        if (c == '\n' || llen >= 1023) {
            line[llen] = '\0';
            if (my_strstr(line, pattern)) {
                sys_write(1, line, (uint64_t)llen);
                sys_write(1, "\n", 1);
            }
            llen = 0;
        } else {
            line[llen++] = c;
        }
    }
    if (llen > 0) {
        line[llen] = '\0';
        if (my_strstr(line, pattern)) {
            sys_write(1, line, (uint64_t)llen);
            sys_write(1, "\n", 1);
        }
    }
    sys_close((int)fd);
    sys_exit(0);
    return 0;
}
