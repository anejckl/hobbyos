#include "syscall.h"
#include "ulib.h"

static void test_epoll_pipe(void) {
    int fds[2];
    if (sys_pipe(fds) < 0) {
        print("FAIL: pipe\n");
        return;
    }

    int epfd = (int)sys_epoll_create();
    if (epfd < 0) {
        print("FAIL: epoll_create\n");
        sys_close(fds[0]);
        sys_close(fds[1]);
        return;
    }

    /* Add read-end to epoll */
    int r = (int)sys_epoll_ctl(epfd, EPOLL_CTL_ADD, fds[0],
                                EPOLLIN, (uint64_t)fds[0]);
    if (r < 0) {
        print("FAIL: epoll_ctl ADD\n");
        sys_close(epfd);
        sys_close(fds[0]);
        sys_close(fds[1]);
        return;
    }

    /* Zero-timeout: nothing ready yet */
    struct epoll_event evs[4];
    int n = (int)sys_epoll_wait(epfd, evs, 4, 0);
    print(n == 0 ? "PASS: epoll_wait zero-timeout returns 0\n"
                 : "FAIL: epoll_wait zero-timeout (expected 0)\n");

    /* Write to write-end */
    sys_write(fds[1], "x", 1);

    /* Now should get 1 event */
    n = (int)sys_epoll_wait(epfd, evs, 4, 100);
    if (n == 1 && (evs[0].events & EPOLLIN) && evs[0].data == (uint64_t)fds[0]) {
        print("PASS: epoll EPOLLIN on pipe\n");
    } else {
        print("FAIL: epoll EPOLLIN on pipe\n");
    }

    /* DEL the fd */
    r = (int)sys_epoll_ctl(epfd, EPOLL_CTL_DEL, fds[0], 0, 0);
    print(r == 0 ? "PASS: epoll_ctl DEL\n" : "FAIL: epoll_ctl DEL\n");

    /* After DEL: zero-timeout returns 0 */
    n = (int)sys_epoll_wait(epfd, evs, 4, 0);
    print(n == 0 ? "PASS: epoll_wait after DEL returns 0\n"
                 : "FAIL: epoll_wait after DEL\n");

    sys_close(epfd);
    sys_close(fds[0]);
    sys_close(fds[1]);
}

static void test_epoll_timeout(void) {
    int epfd = (int)sys_epoll_create();
    if (epfd < 0) {
        print("FAIL: epoll_create for timeout test\n");
        return;
    }

    struct epoll_event evs[4];
    int n = (int)sys_epoll_wait(epfd, evs, 4, 50);
    print(n == 0 ? "PASS: epoll_wait timeout returns 0\n"
                 : "FAIL: epoll_wait timeout (expected 0)\n");

    sys_close(epfd);
}

void _start(void) {
    print("=== epoll_test ===\n");
    test_epoll_pipe();
    test_epoll_timeout();
    print("=== epoll_test done ===\n");
    sys_exit(0);
}
