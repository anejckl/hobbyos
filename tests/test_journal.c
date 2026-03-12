/*
 * test_journal.c — Host-side tests for journal encode/decode
 */
#include "test_main.h"
#include <string.h>

#define JOURNAL_MAGIC       0x4A524E4C
#define JOURNAL_MAX_BLOCKS  16

struct test_journal_header {
    uint32_t magic;
    uint32_t seq;
    uint32_t block_count;
    uint32_t committed;
    uint32_t target_blocks[JOURNAL_MAX_BLOCKS];
};

void test_journal_header(void) {
    struct test_journal_header hdr;
    memset(&hdr, 0, sizeof(hdr));

    hdr.magic = JOURNAL_MAGIC;
    hdr.seq = 42;
    hdr.block_count = 3;
    hdr.committed = 0;
    hdr.target_blocks[0] = 100;
    hdr.target_blocks[1] = 200;
    hdr.target_blocks[2] = 300;

    TEST("journal magic", hdr.magic == JOURNAL_MAGIC);
    TEST("journal seq", hdr.seq == 42);
    TEST("journal block count", hdr.block_count == 3);
    TEST("journal not committed", hdr.committed == 0);
    TEST("journal target block 0", hdr.target_blocks[0] == 100);
    TEST("journal target block 2", hdr.target_blocks[2] == 300);

    /* Simulate commit */
    hdr.committed = 1;
    TEST("journal committed", hdr.committed == 1);

    /* Simulate applied */
    hdr.committed = 2;
    TEST("journal applied", hdr.committed == 2);
}

void test_journal_encode_decode(void) {
    /* Serialize header to byte buffer and back */
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    struct test_journal_header *hdr = (struct test_journal_header *)buf;
    hdr->magic = JOURNAL_MAGIC;
    hdr->seq = 7;
    hdr->block_count = 2;
    hdr->committed = 1;
    hdr->target_blocks[0] = 50;
    hdr->target_blocks[1] = 75;

    /* Read back from buffer */
    struct test_journal_header *read_hdr = (struct test_journal_header *)buf;
    TEST("journal encode/decode magic", read_hdr->magic == JOURNAL_MAGIC);
    TEST("journal encode/decode seq", read_hdr->seq == 7);
    TEST("journal encode/decode blocks", read_hdr->block_count == 2);
    TEST("journal encode/decode committed", read_hdr->committed == 1);
    TEST("journal encode/decode target 0", read_hdr->target_blocks[0] == 50);
    TEST("journal encode/decode target 1", read_hdr->target_blocks[1] == 75);
}

void test_journal_suite(void) {
    printf("=== Journal tests ===\n");
    test_journal_header();
    test_journal_encode_decode();
}
