#include "syscall.h"
#include "ulib.h"

/* Test non-blocking socket operations */
void _start(void) {
    sys_write(1, "nonblock_test: testing non-blocking sockets...\n", 48);

    /* Create a UDP socket */
    int64_t fd = sys_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        sys_write(1, "FAIL: socket() failed\n", 22);
        sys_exit(1);
    }

    /* Bind to a port */
    if (sys_bind((int)fd, 0, 9999) < 0) {
        sys_write(1, "FAIL: bind() failed\n", 20);
        sys_exit(1);
    }

    /* Set non-blocking */
    if (sys_fcntl((int)fd, F_SETFL, O_NONBLOCK) < 0) {
        sys_write(1, "FAIL: fcntl(F_SETFL) failed\n", 28);
        sys_exit(1);
    }

    /* Verify flags */
    int64_t flags = sys_fcntl((int)fd, F_GETFL, 0);
    if (!(flags & O_NONBLOCK)) {
        sys_write(1, "FAIL: O_NONBLOCK not set\n", 25);
        sys_exit(1);
    }

    /* recv should return -EAGAIN (no data) */
    char buf[64];
    int64_t r = sys_recv((int)fd, buf, 64, 0);
    /* Note: -EAGAIN = -11 */
    if (r != -11) {
        sys_write(1, "FAIL: recv didn't return EAGAIN\n", 32);
        sys_exit(1);
    }

    sys_close((int)fd);
    sys_write(1, "nonblock_test: PASS\n", 20);
    sys_exit(0);
}
