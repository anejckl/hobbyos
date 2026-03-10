#include "syscall.h"
#include "ulib.h"

static void test_anon_mmap(void) {
    /* Anonymous MAP_PRIVATE mmap: write and read back */
    void *m = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        print("FAIL: anon mmap returned MAP_FAILED\n");
        return;
    }

    char *p = (char *)m;
    int i;
    for (i = 0; i < 4096; i++) p[i] = (char)(i & 0xff);

    int ok = 1;
    for (i = 0; i < 4096; i++) {
        if (p[i] != (char)(i & 0xff)) { ok = 0; break; }
    }
    print(ok ? "PASS: anon mmap write/read\n" : "FAIL: anon mmap data mismatch\n");

    int64_t r = sys_munmap(m, 4096);
    print(r == 0 ? "PASS: munmap\n" : "FAIL: munmap\n");
}

static void test_fork_private(void) {
    /* MAP_PRIVATE: child write doesn't affect parent */
    void *m = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        print("FAIL: fork_private mmap\n");
        return;
    }

    char *p = (char *)m;
    p[0] = 42;

    int64_t pid = sys_fork();
    if (pid == 0) {
        /* child */
        p[0] = 99;
        sys_exit(0);
    }

    int32_t status;
    sys_wait(&status);

    print(p[0] == 42 ? "PASS: MAP_PRIVATE fork isolation\n"
                      : "FAIL: MAP_PRIVATE fork isolation\n");
    sys_munmap(m, 4096);
}

static void test_brk(void) {
    uint64_t cur = sys_brk(0);
    if (cur == 0 || cur == (uint64_t)-1) {
        print("FAIL: brk(0)\n");
        return;
    }

    uint64_t new_brk = cur + 4096;
    uint64_t result = sys_brk(new_brk);
    if (result < new_brk) {
        print("FAIL: brk grow\n");
        return;
    }

    char *heap = (char *)cur;
    heap[0] = (char)0x5a;
    heap[4095] = (char)0xa5;
    int ok = (heap[0] == (char)0x5a && heap[4095] == (char)0xa5);
    print(ok ? "PASS: brk heap extend\n" : "FAIL: brk heap data\n");
}

void _start(void) {
    print("=== mmap_test ===\n");
    test_anon_mmap();
    test_fork_private();
    test_brk();
    print("=== mmap_test done ===\n");
    sys_exit(0);
}
