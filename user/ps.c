#include "ulib.h"

void _start(void) {
    print("PID  STATUS\n");
    print("---- ------\n");

    /* Read /proc/<pid>/status for PIDs 1..64 */
    char path[32];
    char buf[256];

    for (int pid = 1; pid <= 64; pid++) {
        /* Build path: /proc/<pid>/status */
        path[0] = '/'; path[1] = 'p'; path[2] = 'r'; path[3] = 'o';
        path[4] = 'c'; path[5] = '/';
        int pos = 6;
        /* Write PID digits */
        if (pid >= 10) {
            path[pos++] = '0' + (char)(pid / 10);
        }
        path[pos++] = '0' + (char)(pid % 10);
        path[pos++] = '/';
        path[pos++] = 's'; path[pos++] = 't'; path[pos++] = 'a';
        path[pos++] = 't'; path[pos++] = 'u'; path[pos++] = 's';
        path[pos] = '\0';

        int fd = (int)sys_open(path, 0);
        if (fd < 0)
            continue;

        int64_t n = sys_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            sys_write(1, buf, (uint64_t)n);
            if (buf[n - 1] != '\n')
                print("\n");
        }
        sys_close(fd);
    }

    sys_exit(0);
}
