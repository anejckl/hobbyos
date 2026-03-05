/*
 * test_string.c — Unit tests for kernel/string.c
 *
 * We include the kernel's string.c directly so we test the actual
 * kernel implementation, not libc.
 */

/* Block kernel/common.h (uses inline asm) — stubs.h provides the types */
#define COMMON_H
#include "stubs.h"
#include "test_main.h"

/* Include the actual kernel string implementation */
#include "../kernel/string.c"

/* ---- strlen ---- */

void test_strlen(void) {
    TEST("strlen empty", strlen("") == 0);
    TEST("strlen short", strlen("abc") == 3);
    TEST("strlen longer", strlen("hello world") == 11);
}

/* ---- strcmp ---- */

void test_strcmp(void) {
    TEST("strcmp equal", strcmp("abc", "abc") == 0);
    TEST("strcmp less", strcmp("abc", "abd") < 0);
    TEST("strcmp greater", strcmp("abd", "abc") > 0);
    TEST("strcmp empty", strcmp("", "") == 0);
    TEST("strcmp prefix", strcmp("ab", "abc") < 0);
}

/* ---- strncmp ---- */

void test_strncmp(void) {
    TEST("strncmp equal prefix", strncmp("abcdef", "abcxyz", 3) == 0);
    TEST("strncmp differ", strncmp("abcdef", "abcxyz", 4) != 0);
    TEST("strncmp zero n", strncmp("abc", "xyz", 0) == 0);
    TEST("strncmp short string", strncmp("ab", "ab", 10) == 0);
}

/* ---- strcpy ---- */

void test_strcpy(void) {
    char buf[32];
    strcpy(buf, "hello");
    TEST("strcpy basic", strcmp(buf, "hello") == 0);

    strcpy(buf, "");
    TEST("strcpy empty", strcmp(buf, "") == 0);
}

/* ---- strncpy ---- */

void test_strncpy(void) {
    char buf[16];

    /* Basic copy with room to spare */
    memset(buf, 'X', sizeof(buf));
    strncpy(buf, "abc", 8);
    TEST("strncpy basic", strcmp(buf, "abc") == 0);
    /* Should null-pad remaining bytes */
    TEST("strncpy null pad", buf[4] == '\0' && buf[7] == '\0');

    /* Truncation: n shorter than source */
    memset(buf, 'X', sizeof(buf));
    strncpy(buf, "hello world", 5);
    TEST("strncpy truncate", memcmp(buf, "hello", 5) == 0);
}

/* ---- strtok ---- */

void test_strtok(void) {
    char str[] = "hello world foo";
    char *tok;

    tok = strtok(str, " ");
    TEST("strtok first", tok != NULL && strcmp(tok, "hello") == 0);
    tok = strtok(NULL, " ");
    TEST("strtok second", tok != NULL && strcmp(tok, "world") == 0);
    tok = strtok(NULL, " ");
    TEST("strtok third", tok != NULL && strcmp(tok, "foo") == 0);
    tok = strtok(NULL, " ");
    TEST("strtok end", tok == NULL);

    /* Consecutive delimiters */
    char str2[] = "a,,b,,c";
    tok = strtok(str2, ",");
    TEST("strtok consec first", tok != NULL && strcmp(tok, "a") == 0);
    tok = strtok(NULL, ",");
    TEST("strtok consec second", tok != NULL && strcmp(tok, "b") == 0);
    tok = strtok(NULL, ",");
    TEST("strtok consec third", tok != NULL && strcmp(tok, "c") == 0);
    tok = strtok(NULL, ",");
    TEST("strtok consec end", tok == NULL);
}

/* ---- memset ---- */

void test_memset(void) {
    char buf[16];
    memset(buf, 0xAA, 8);
    int ok = 1;
    for (int i = 0; i < 8; i++)
        if ((unsigned char)buf[i] != 0xAA) ok = 0;
    TEST("memset fill", ok);

    memset(buf, 0, 16);
    ok = 1;
    for (int i = 0; i < 16; i++)
        if (buf[i] != 0) ok = 0;
    TEST("memset zero", ok);
}

/* ---- memcpy ---- */

void test_memcpy(void) {
    char src[] = "abcdefgh";
    char dst[16] = {0};
    memcpy(dst, src, 8);
    TEST("memcpy basic", memcmp(dst, src, 8) == 0);
}

/* ---- memmove ---- */

void test_memmove(void) {
    /* Non-overlapping */
    char buf[16] = "abcdefgh";
    char dst[16] = {0};
    memmove(dst, buf, 8);
    TEST("memmove non-overlap", memcmp(dst, "abcdefgh", 8) == 0);

    /* Overlapping: shift right */
    char overlap[] = "abcdefgh";
    memmove(overlap + 2, overlap, 6);
    TEST("memmove overlap right", memcmp(overlap + 2, "abcdef", 6) == 0);

    /* Overlapping: shift left */
    char overlap2[] = "abcdefgh";
    memmove(overlap2, overlap2 + 2, 6);
    TEST("memmove overlap left", memcmp(overlap2, "cdefgh", 6) == 0);
}

/* ---- memcmp ---- */

void test_memcmp(void) {
    TEST("memcmp equal", memcmp("abcd", "abcd", 4) == 0);
    TEST("memcmp differ", memcmp("abcd", "abce", 4) != 0);
    TEST("memcmp less", memcmp("abcd", "abce", 4) < 0);
    TEST("memcmp zero", memcmp("abc", "xyz", 0) == 0);
}

/* ---- Suite entry point ---- */

void test_string_suite(void) {
    printf("=== String tests ===\n");
    test_strlen();
    test_strcmp();
    test_strncmp();
    test_strcpy();
    test_strncpy();
    test_strtok();
    test_memset();
    test_memcpy();
    test_memmove();
    test_memcmp();
}
