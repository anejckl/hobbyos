#include "ulib.h"

void _start(void) {
    const char *path = get_argv();
    if (!path)
        path = "/";

    int fd = (int)sys_open(path, 0);
    if (fd < 0) {
        print("ls: cannot open ");
        print(path);
        print("\n");
        sys_exit(1);
    }

    uint8_t buf[512];
    int64_t n = sys_getdents(fd, buf, sizeof(buf));
    if (n > 0) {
        uint32_t pos = 0;
        while (pos < (uint32_t)n) {
            /* Skip inode (4 bytes) */
            uint8_t name_len = buf[pos + 4];
            if (name_len == 0)
                break;
            sys_write(1, (const char *)(buf + pos + 5), name_len);
            print("\n");
            pos += 5 + name_len;
        }
    }

    sys_close(fd);
    sys_exit(0);
}
