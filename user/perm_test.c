#include "syscall.h"
#include "ulib.h"

/* Test credential syscalls and permission system */
void _start(void) {
    sys_write(1, "perm_test: testing credentials...\n", 34);

    /* Get current uid/gid (should be root = 0) */
    uint64_t uid = sys_getuid();
    uint64_t gid = sys_getgid();

    if (uid != 0) {
        sys_write(1, "FAIL: expected uid=0\n", 21);
        sys_exit(1);
    }
    if (gid != 0) {
        sys_write(1, "FAIL: expected gid=0\n", 21);
        sys_exit(1);
    }

    /* Root should be able to setuid to any value */
    if (sys_setuid(1000) < 0) {
        sys_write(1, "FAIL: root setuid(1000) failed\n", 31);
        sys_exit(1);
    }

    /* Now uid should be 1000 */
    uid = sys_getuid();
    if (uid != 1000) {
        sys_write(1, "FAIL: uid not 1000 after setuid\n", 32);
        sys_exit(1);
    }

    /* Set back to root (euid is still 0) */
    if (sys_seteuid(0) < 0) {
        sys_write(1, "FAIL: seteuid(0) failed\n", 24);
        sys_exit(1);
    }

    sys_write(1, "perm_test: PASS\n", 16);
    sys_exit(0);
}
