#include "ulib.h"

void _start(void) {
    const char *args = get_argv();
    if (args) {
        print(args);
    }
    print("\n");
    sys_exit(0);
}
