#include "ulib.h"

void _start(void) {
    const char *path = get_argv();
    if (!path) {
        print("usage: touch <path>\n");
        sys_exit(1);
    }

    int fd = (int)sys_open(path, O_CREAT);
    if (fd < 0) {
        print("touch: failed to create ");
        print(path);
        print("\n");
        sys_exit(1);
    }
    sys_close(fd);
    sys_exit(0);
}
