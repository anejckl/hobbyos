#include "syscall.h"
#include "ulib.h"

void _start(void) {
    print("net_test: starting\n");

    /* Test 1: Create TCP socket */
    int tcp_fd = (int)sys_socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) {
        print("net_test: FAIL - socket(STREAM) returned error\n");
        sys_exit(1);
    }
    print("net_test: socket(STREAM) = ");
    print_num((uint64_t)tcp_fd);
    print("\n");

    /* Test 2: Create UDP socket */
    int udp_fd = (int)sys_socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        print("net_test: FAIL - socket(DGRAM) returned error\n");
        sys_exit(1);
    }
    print("net_test: socket(DGRAM) = ");
    print_num((uint64_t)udp_fd);
    print("\n");

    /* Test 3: Bind UDP socket */
    if (sys_bind(udp_fd, 0, 5555) < 0) {
        print("net_test: FAIL - bind() returned error\n");
        sys_exit(1);
    }
    print("net_test: bind(5555) OK\n");

    /* Test 4: Close sockets */
    sys_close(tcp_fd);
    sys_close(udp_fd);
    print("net_test: close() OK\n");

    print("net_test: PASS - all socket tests passed\n");
    sys_exit(0);
}
