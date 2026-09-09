/* Minimal CRT0 for TCC on HobbyOS.
 * The kernel's ELF loader enters ring 3 with argc in RDI and argv in RSI
 * as a genuine custom convention (see kernel/process/user_process.c's
 * user_trampoline: "Enter ring 3 with argc in RDI, argv ptr in RSI"), so
 * _start declared as a normal two-parameter C function receives them
 * exactly as a standard SysV call would — no raw stack parsing needed. */
#include "../lib/libc.h"

int main(int argc, char **argv);

void _start(int64_t argc, char **argv) {
    int ret = main((int)argc, argv);
    sys_exit(ret);
}
