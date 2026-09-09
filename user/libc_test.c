#include "lib/libc.h"

#define RECURSION_DEPTH 800
#define PAD_SIZE        200

static volatile int recursion_sink = 0;

static long recurse(int depth) {
    volatile char padding[PAD_SIZE];
    for (int i = 0; i < PAD_SIZE; i++) padding[i] = (char)(depth + i);
    if (depth == 0) return padding[0];
    return recurse(depth - 1) + padding[0];
}

static int test_malloc_stability(void) {
    /* Warm up: one alloc+free of the largest size used below establishes a
     * single free block big enough for every subsequent request. Measuring
     * brk before this warm-up would count the pool's one-time initial
     * growth as a "leak" — backward coalescing prevents *unbounded* growth
     * across repeated cycles, it doesn't eliminate that necessary first
     * allocation. */
    void *warm = malloc(248);
    if (!warm) {
        printf("FAIL: malloc warm-up allocation failed\n");
        return 0;
    }
    free(warm);

    uint64_t brk_before = sys_brk_libc(0);
    for (int iter = 0; iter < 2000; iter++) {
        size_t sz = (size_t)((iter % 11) * 24 + 8);
        void *p = malloc(sz);
        if (!p) {
            printf("FAIL: malloc stress allocation failed at iter %d\n", iter);
            return 0;
        }
        memset(p, iter & 0xff, sz);
        free(p);
    }
    uint64_t brk_after = sys_brk_libc(0);
    if (brk_after > brk_before) {
        printf("FAIL: malloc stress brk grew (heap fragmentation)\n");
        return 0;
    }
    printf("PASS: malloc stress brk stable\n");
    return 1;
}

static int test_file_roundtrip(void) {
    const char *path = "/libc_test_tmp.txt";
    const char *content = "hello from FILE* on ext2";
    size_t content_len = strlen(content);

    FILE *f = fopen(path, "w");
    if (!f) {
        printf("FAIL: fopen for write failed\n");
        return 0;
    }
    size_t written = fwrite(content, 1, content_len, f);
    fclose(f);
    if (written != content_len) {
        printf("FAIL: fwrite short write\n");
        return 0;
    }

    FILE *f2 = fopen(path, "r");
    if (!f2) {
        printf("FAIL: fopen for read failed\n");
        return 0;
    }
    char buf[128];
    memset(buf, 0, sizeof(buf));
    size_t got = fread(buf, 1, content_len, f2);
    fclose(f2);

    if (got != content_len || strcmp(buf, content) != 0) {
        printf("FAIL: fread mismatch (got %d bytes): %s\n", (int)got, buf);
        return 0;
    }
    printf("PASS: FILE round trip matches\n");
    return 1;
}

static int test_deep_recursion(void) {
    long result = recurse(RECURSION_DEPTH);
    recursion_sink = (int)result;
    printf("PASS: deep recursion to depth %d completed\n", RECURSION_DEPTH);
    return 1;
}

void _start(void) {
    int ok = 1;
    ok &= test_malloc_stability();
    ok &= test_file_roundtrip();
    ok &= test_deep_recursion();

    if (ok) {
        printf("libc_test: all tests passed\n");
        sys_exit(0);
    } else {
        printf("libc_test: FAILED\n");
        sys_exit(1);
    }
}
