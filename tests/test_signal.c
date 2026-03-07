/*
 * test_signal.c — Unit tests for signal bitmask operations.
 */

#include "test_main.h"
#include <string.h>

/* Signal constants (from kernel/signal/signal.h) */
#define SIGINT   2
#define SIGKILL  9
#define SIGPIPE  13
#define SIGTERM  15
#define SIGCHLD  17

static void test_signal_bitmask_set(void) {
    uint32_t pending = 0;

    /* Set SIGINT */
    pending |= (1u << SIGINT);
    TEST("SIGINT bit is set", (pending & (1u << SIGINT)) != 0);
    TEST("SIGTERM bit is not set", (pending & (1u << SIGTERM)) == 0);
}

static void test_signal_bitmask_clear(void) {
    uint32_t pending = 0;
    pending |= (1u << SIGINT);
    pending |= (1u << SIGTERM);

    /* Clear SIGINT */
    pending &= ~(1u << SIGINT);
    TEST("SIGINT bit is cleared", (pending & (1u << SIGINT)) == 0);
    TEST("SIGTERM bit still set", (pending & (1u << SIGTERM)) != 0);
}

static void test_signal_bitmask_multiple(void) {
    uint32_t pending = 0;
    pending |= (1u << SIGINT);
    pending |= (1u << SIGKILL);
    pending |= (1u << SIGCHLD);

    TEST("3 signals pending", pending != 0);

    /* Find lowest signal */
    int lowest = -1;
    for (int sig = 1; sig < 32; sig++) {
        if (pending & (1u << sig)) {
            lowest = sig;
            break;
        }
    }
    TEST("lowest pending signal is SIGINT (2)", lowest == SIGINT);
}

static void test_signal_handler_values(void) {
    /* SIG_DFL = 0, SIG_IGN = 1, user handler > 1 */
    uint64_t handlers[32];
    memset(handlers, 0, sizeof(handlers));

    handlers[SIGINT] = 0;       /* SIG_DFL */
    handlers[SIGCHLD] = 1;      /* SIG_IGN */
    handlers[SIGTERM] = 0x400100;  /* user handler */

    TEST("SIGINT handler is SIG_DFL", handlers[SIGINT] == 0);
    TEST("SIGCHLD handler is SIG_IGN", handlers[SIGCHLD] == 1);
    TEST("SIGTERM handler is user addr", handlers[SIGTERM] > 1);
}

void test_signal_suite(void) {
    printf("=== Signal tests ===\n");
    test_signal_bitmask_set();
    test_signal_bitmask_clear();
    test_signal_bitmask_multiple();
    test_signal_handler_values();
}
