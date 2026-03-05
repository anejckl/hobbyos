/*
 * test_main.c — Minimal test runner for HobbyOS host-side unit tests.
 *
 * No external dependencies — just compile and run.
 * Exit code: 0 if all tests pass, 1 if any fail.
 */

#include "test_main.h"

int tests_passed = 0;
int tests_failed = 0;

int main(void) {
    printf("HobbyOS Host Unit Tests\n");
    printf("=======================\n\n");

    test_string_suite();
    printf("\n");

    test_pmm_suite();
    printf("\n");

    test_printf_suite();
    printf("\n");

    test_elf_suite();
    printf("\n");

    test_vfs_suite();
    printf("\n");

    printf("=======================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           tests_passed, tests_failed, tests_passed + tests_failed);

    if (tests_failed > 0) {
        printf("FAILED\n");
        return 1;
    }

    printf("ALL TESTS PASSED\n");
    return 0;
}
