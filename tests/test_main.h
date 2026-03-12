/*
 * test_main.h — Minimal test harness (no external dependencies).
 */

#ifndef TEST_MAIN_H
#define TEST_MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

/* Counters — defined in test_main.c */
extern int tests_passed;
extern int tests_failed;

/* Test macro: evaluates expr and prints PASS/FAIL */
#define TEST(name, expr) do { \
    if (expr) { \
        printf("  PASS: %s\n", name); \
        tests_passed++; \
    } else { \
        printf("  FAIL: %s\n", name); \
        tests_failed++; \
    } \
} while (0)

/* Suite declarations */
void test_string_suite(void);
void test_pmm_suite(void);
void test_refcount_suite(void);
void test_printf_suite(void);
void test_elf_suite(void);
void test_vfs_suite(void);
void test_pipe_suite(void);
void test_signal_suite(void);
void test_netbuf_suite(void);
void test_checksum_suite(void);
void test_device_suite(void);
void test_cred_suite(void);
void test_bcache_suite(void);
void test_swap_suite(void);
void test_journal_suite(void);

#endif /* TEST_MAIN_H */
