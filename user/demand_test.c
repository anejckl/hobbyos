#include "syscall.h"
#include "ulib.h"

/* Test demand paging: allocate a large sparse mmap and verify data integrity */
void _start(void) {
    sys_write(1, "demand_test: testing demand paging...\n", 38);

    /* Allocate 64KB anonymous mapping */
    void *ptr = sys_mmap(NULL, 65536, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        sys_write(1, "FAIL: mmap failed\n", 18);
        sys_exit(1);
    }

    /* Write to first and last page only (sparse access) */
    char *p = (char *)ptr;
    p[0] = 'A';
    p[4096] = 'B';     /* second page */
    p[65536 - 1] = 'Z'; /* last byte of last page */

    /* Verify */
    if (p[0] != 'A' || p[4096] != 'B' || p[65536 - 1] != 'Z') {
        sys_write(1, "FAIL: data mismatch\n", 20);
        sys_exit(1);
    }

    /* Middle pages should be zero (never written) */
    if (p[8192] != 0 || p[12288] != 0) {
        sys_write(1, "FAIL: unwritten pages not zero\n", 31);
        sys_exit(1);
    }

    sys_write(1, "demand_test: PASS\n", 18);
    sys_munmap(ptr, 65536);
    sys_exit(0);
}
