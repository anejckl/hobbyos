/*
 * test_pipe.c — Unit tests for pipe ring buffer logic.
 *
 * Tests the pipe data structure directly (no scheduling/blocking).
 */

/* Block kernel headers FIRST */
#define COMMON_H
#define STRING_H
#define DEBUG_H
#define SCHEDULER_H
#define KHEAP_H

#include "stubs.h"
#include "test_main.h"
#include <string.h>
#include <stdlib.h>

/* Stub debug_printf */
#define debug_printf pipe_test_debug_printf
static void pipe_test_debug_printf(const char *fmt, ...) { (void)fmt; }

/* Stub scheduler functions */
struct process;
static struct process *scheduler_get_current(void) { return NULL; }
static void scheduler_add(struct process *p) { (void)p; }
static void schedule(void) {}

/* Stub kmalloc */
static void *kmalloc(uint64_t size) { return malloc((size_t)size); }

/* Define FD types needed by pipe.c */
#define FD_PIPE_READ  2
#define FD_PIPE_WRITE 3

/* Include pipe source directly */
#include "../kernel/fs/pipe.c"

#undef debug_printf

static void test_pipe_create(void) {
    struct pipe *p = NULL;
    int ret = pipe_create(&p);
    TEST("pipe_create returns 0", ret == 0);
    TEST("pipe_create sets non-NULL pointer", p != NULL);
    TEST("pipe initial count is 0", p->count == 0);
    TEST("pipe initial readers is 1", p->readers == 1);
    TEST("pipe initial writers is 1", p->writers == 1);
    free(p);
}

static void test_pipe_write_read(void) {
    struct pipe *p = NULL;
    pipe_create(&p);

    const uint8_t data[] = "Hello, pipe!";
    int written = pipe_write(p, data, 12);
    TEST("pipe_write returns 12", written == 12);
    TEST("pipe count after write is 12", p->count == 12);

    uint8_t buf[32] = {0};
    int rd = pipe_read(p, buf, 32);
    TEST("pipe_read returns 12", rd == 12);
    TEST("pipe_read data matches", memcmp(buf, "Hello, pipe!", 12) == 0);
    TEST("pipe count after read is 0", p->count == 0);
    free(p);
}

static void test_pipe_wrap_around(void) {
    struct pipe *p = NULL;
    pipe_create(&p);

    /* Write PIPE_BUF_SIZE - 10 bytes */
    uint8_t big_buf[PIPE_BUF_SIZE];
    memset(big_buf, 'A', sizeof(big_buf));
    int written = pipe_write(p, big_buf, PIPE_BUF_SIZE - 10);
    TEST("write near-full", written == PIPE_BUF_SIZE - 10);

    /* Read all */
    uint8_t read_buf[PIPE_BUF_SIZE];
    int rd = pipe_read(p, read_buf, PIPE_BUF_SIZE);
    TEST("read all near-full", rd == PIPE_BUF_SIZE - 10);

    /* Now write_pos and read_pos are both near the end.
     * Write 20 bytes — should wrap around. */
    memset(big_buf, 'B', 20);
    written = pipe_write(p, big_buf, 20);
    TEST("write wrapping around succeeds", written == 20);

    rd = pipe_read(p, read_buf, 20);
    TEST("read wrapping data returns 20", rd == 20);
    TEST("read wrapping data is correct", read_buf[0] == 'B' && read_buf[19] == 'B');
    free(p);
}

static void test_pipe_eof(void) {
    struct pipe *p = NULL;
    pipe_create(&p);

    /* Close write end */
    pipe_close_write(p);
    TEST("writers is 0 after close_write", p->writers == 0);

    /* Read should return 0 (EOF) */
    uint8_t buf[16];
    int rd = pipe_read(p, buf, 16);
    TEST("read on empty pipe with no writers returns 0 (EOF)", rd == 0);
    free(p);
}

static void test_pipe_close_refcount(void) {
    struct pipe *p = NULL;
    pipe_create(&p);

    pipe_inc_ref(p, FD_PIPE_READ);
    TEST("readers is 2 after inc_ref", p->readers == 2);

    pipe_close_read(p);
    TEST("readers is 1 after one close", p->readers == 1);

    pipe_close_read(p);
    TEST("readers is 0 after second close", p->readers == 0);
    free(p);
}

void test_pipe_suite(void) {
    printf("=== Pipe tests ===\n");
    test_pipe_create();
    test_pipe_write_read();
    test_pipe_wrap_around();
    test_pipe_eof();
    test_pipe_close_refcount();
}
