#include "ulib.h"

void _start(void) {
    const char *path = get_argv();
    if (!path) {
        print("usage: rm <path>\n");
        sys_exit(1);
    }

    if (sys_unlink(path) < 0) {
        print("rm: failed to remove ");
        print(path);
        print("\n");
        sys_exit(1);
    }

    sys_exit(0);
}
