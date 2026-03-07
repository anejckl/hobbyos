#include "ulib.h"

void _start(void) {
    const char *path = get_argv();
    if (!path) {
        print("usage: mkdir <path>\n");
        sys_exit(1);
    }

    if (sys_mkdir(path) < 0) {
        print("mkdir: failed to create ");
        print(path);
        print("\n");
        sys_exit(1);
    }

    sys_exit(0);
}
