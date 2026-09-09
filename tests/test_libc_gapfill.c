/*
 * test_libc_gapfill.c — Unit tests for the TCC-port libc gap-fill functions
 * added to user/lib/string.c and user/lib/stdlib.c.
 *
 * Includes the actual user-space implementations directly (same technique
 * as test_string.c for kernel/string.c) so we test the real code, not a
 * reimplementation. user/lib/libc.h's own guard is pre-defined so its
 * typedefs (which would collide with the real <stdint.h> types already
 * pulled in by test_main.h) are skipped — string.c/stdlib.c fall back to
 * those real system types instead, which are ABI-compatible for these
 * pure-algorithm functions. malloc.c is deliberately NOT included here
 * (it depends on the sys_brk_libc syscall wrapper) — strdup below exercises
 * the host's real malloc/free instead, which is sufficient to verify its
 * copy-and-terminate logic; malloc.c's own backward-coalescing fix is
 * covered by the interactive QEMU test suite instead.
 */

#define LIBC_H

#include "test_main.h"

/* user/lib/string.c defines several functions that also exist in
 * kernel/string.c (already linked into this same test binary via
 * test_string.c) — rename them consistently so both copies can coexist.
 * The #define applies to every use for the rest of this file, including
 * this file's own test bodies below and the new functions' internal calls
 * (e.g. strdup calling strlen/memcpy), so nothing else needs to change. */
#define strlen   strlen_ul
#define strcpy   strcpy_ul
#define strncpy  strncpy_ul
#define strcmp   strcmp_ul
#define strncmp  strncmp_ul
#define memcpy   memcpy_ul
#define memset   memset_ul
#define memmove  memmove_ul
#define memcmp   memcmp_ul
#define strstr   strstr_ul
#define strchr   strchr_ul
#define strrchr  strrchr_ul
#define strnlen  strnlen_ul

#include "../user/lib/string.c"

/* stdlib.c's atol/exit/abort collide with the real host <stdlib.h>
 * (different atol return type; exit/abort call the undeclared sys_exit) —
 * none of those three are under test here, so rename them out of the way
 * rather than duplicating the rest of the file's logic. */
static void sys_exit(int status) { (void)status; }
#define atol  atol_hobbyos_unused
#define exit  exit_hobbyos_unused
#define abort abort_hobbyos_unused
#include "../user/lib/stdlib.c"
#undef atol
#undef exit
#undef abort

/* ---- memmove ---- */

static void test_memmove(void) {
    char buf[16];

    /* Forward overlap (dst > src) */
    strcpy(buf, "abcdefgh");
    memmove(buf + 2, buf, 5);
    TEST("memmove forward overlap", strncmp(buf + 2, "abcde", 5) == 0);

    /* Backward overlap (dst < src) */
    strcpy(buf, "abcdefgh");
    memmove(buf, buf + 2, 5);
    TEST("memmove backward overlap", strncmp(buf, "cdefg", 5) == 0);

    /* Non-overlapping */
    char src[8] = "12345";
    char dst[8] = {0};
    memmove(dst, src, 6);
    TEST("memmove non-overlapping", strcmp(dst, "12345") == 0);

    /* Zero length is a no-op */
    strcpy(buf, "unchanged");
    memmove(buf, buf + 1, 0);
    TEST("memmove zero length", strcmp(buf, "unchanged") == 0);
}

/* ---- memcmp ---- */

static void test_memcmp(void) {
    TEST("memcmp equal", memcmp("abc", "abc", 3) == 0);
    TEST("memcmp less", memcmp("abc", "abd", 3) < 0);
    TEST("memcmp greater", memcmp("abd", "abc", 3) > 0);
    TEST("memcmp zero length", memcmp("abc", "xyz", 0) == 0);
    /* Bytes beyond n must not be examined */
    TEST("memcmp respects length", memcmp("abcX", "abcY", 3) == 0);
}

/* ---- strcat / strncat ---- */

static void test_strcat(void) {
    char buf[32] = "Hello, ";
    strcat(buf, "World!");
    TEST("strcat basic", strcmp(buf, "Hello, World!") == 0);

    char buf2[32] = "foo";
    strcat(buf2, "");
    TEST("strcat empty suffix", strcmp(buf2, "foo") == 0);

    char buf3[32] = "abc";
    strncat(buf3, "defgh", 3);
    TEST("strncat truncates", strcmp(buf3, "abcdef") == 0);

    char buf4[32] = "abc";
    strncat(buf4, "de", 10);
    TEST("strncat shorter than n", strcmp(buf4, "abcde") == 0);
}

/* ---- strdup ---- */

static void test_strdup(void) {
    const char *original = "duplicate me";
    char *copy = strdup(original);
    TEST("strdup non-null", copy != NULL);
    TEST("strdup content matches", strcmp(copy, original) == 0);
    TEST("strdup is a distinct buffer", copy != original);
    /* Mutating the copy must not affect the original */
    copy[0] = 'D';
    TEST("strdup independent buffer", original[0] == 'd');
    free(copy);
}

/* ---- strtol / strtoul ---- */

static void test_strtol(void) {
    TEST("strtol decimal", strtol("42", NULL, 10) == 42);
    TEST("strtol negative", strtol("-17", NULL, 10) == -17);
    TEST("strtol plus sign", strtol("+5", NULL, 10) == 5);
    TEST("strtol hex 0x prefix, base 0", strtol("0x1F", NULL, 0) == 31);
    TEST("strtol hex explicit base", strtol("1F", NULL, 16) == 31);
    TEST("strtol octal, base 0", strtol("017", NULL, 0) == 15);
    TEST("strtol leading whitespace", strtol("   9", NULL, 10) == 9);

    char *end = NULL;
    strtol("123abc", &end, 10);
    TEST("strtol endptr stops at non-digit", end != NULL && *end == 'a');

    TEST("strtoul decimal", strtoul("42", NULL, 10) == 42UL);
    TEST("strtoul hex 0x prefix, base 0", strtoul("0xFF", NULL, 0) == 255UL);
}

/* ---- qsort ---- */

static int int_cmp(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

static void test_qsort(void) {
    int arr[] = { 5, 3, 8, 1, 9, 2, 7, 4, 6, 0 };
    int n = (int)(sizeof(arr) / sizeof(arr[0]));
    qsort(arr, (size_t)n, sizeof(int), int_cmp);

    int sorted = 1;
    for (int i = 1; i < n; i++) {
        if (arr[i - 1] > arr[i]) sorted = 0;
    }
    TEST("qsort produces ascending order", sorted);
    TEST("qsort first element", arr[0] == 0);
    TEST("qsort last element", arr[n - 1] == 9);

    /* Already-sorted input (would be adversarial for a naive first-element
     * pivot quicksort) must still terminate and stay correct. */
    int already_sorted[20];
    for (int i = 0; i < 20; i++) already_sorted[i] = i;
    qsort(already_sorted, 20, sizeof(int), int_cmp);
    int still_sorted = 1;
    for (int i = 1; i < 20; i++) {
        if (already_sorted[i - 1] > already_sorted[i]) still_sorted = 0;
    }
    TEST("qsort handles pre-sorted input", still_sorted);

    /* Single element and empty array must not crash */
    int one[] = { 42 };
    qsort(one, 1, sizeof(int), int_cmp);
    TEST("qsort single element", one[0] == 42);
    qsort(one, 0, sizeof(int), int_cmp);
    TEST("qsort zero elements is a no-op", 1);
}

/* ---- getenv ---- */

static void test_getenv(void) {
    /* No process ever receives envp — getenv must always report unset. */
    TEST("getenv always returns NULL", getenv("PATH") == NULL);
    TEST("getenv always returns NULL for anything", getenv("HOME") == NULL);
}

void test_libc_gapfill_suite(void) {
    printf("=== libc gap-fill tests (TCC port prerequisites) ===\n");
    test_memmove();
    test_memcmp();
    test_strcat();
    test_strdup();
    test_strtol();
    test_qsort();
    test_getenv();
}
