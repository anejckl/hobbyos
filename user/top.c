#include "syscall.h"

static int my_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void my_write(const char *s) { sys_write(1, s, (uint64_t)my_strlen(s)); }

static int ntos(uint64_t v, char *buf, int size) {
    if (size <= 1) return 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[20]; int ti = 0;
    while (v > 0 && ti < 19) { tmp[ti++] = '0' + (int)(v % 10); v /= 10; }
    int j = 0;
    while (ti > 0 && j < size - 1) buf[j++] = tmp[--ti];
    buf[j] = '\0';
    return j;
}

int _start(void) {
    char buf[512];
    char path[32];
    char num[8];
    my_write("PID  STATE   NAME\n");
    my_write("---  ------  ----\n");
    for (int pid = 1; pid <= 64; pid++) {
        /* Build /proc/N/status */
        path[0] = '/'; path[1] = 'p'; path[2] = 'r'; path[3] = 'o';
        path[4] = 'c'; path[5] = '/';
        int nl = ntos((uint64_t)pid, num, sizeof(num));
        int k;
        for (k = 0; k < nl; k++) path[6 + k] = num[k];
        path[6 + nl] = '/'; path[7 + nl] = 's'; path[8 + nl] = 't';
        path[9 + nl] = 'a'; path[10 + nl] = 't'; path[11 + nl] = 'u';
        path[12 + nl] = 's'; path[13 + nl] = '\0';

        int64_t fd = sys_open(path, 0);
        if (fd < 0) continue;
        int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
        sys_close((int)fd);
        if (n > 0) {
            buf[n] = '\0';
            sys_write(1, buf, (uint64_t)n);
            sys_write(1, "\n", 1);
        }
    }
    sys_exit(0);
    return 0;
}
