#include "syscall.h"
#include "ulib.h"

void _start(void) {
    const char *args = get_argv();
    if (!args) {
        print("Usage: ping <ip>\n");
        print("Note: Use shell 'ping' command instead (ICMP requires kernel access)\n");
        sys_exit(1);
    }

    print("ping: ICMP requires kernel access.\n");
    print("Use the shell 'ping' command directly.\n");
    sys_exit(0);
}
