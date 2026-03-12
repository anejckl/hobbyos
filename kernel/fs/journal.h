#ifndef JOURNAL_H
#define JOURNAL_H

#include "../common.h"

#define JOURNAL_MAGIC       0x4A524E4C
#define JOURNAL_MAX_BLOCKS  16
#define JOURNAL_START_LBA   65536   /* after ext2 partition (32MB offset) */
#define JOURNAL_AREA_BLOCKS 128     /* 128 blocks = 128KB journal area */

struct journal_txn {
    uint32_t target_blocks[JOURNAL_MAX_BLOCKS];
    uint8_t *block_data[JOURNAL_MAX_BLOCKS];
    int count;
    bool active;
};

/* On-disk journal header */
struct journal_header {
    uint32_t magic;
    uint32_t seq;           /* sequence number */
    uint32_t block_count;
    uint32_t committed;     /* 1 = committed, 0 = in-progress */
    uint32_t target_blocks[JOURNAL_MAX_BLOCKS];
};

/* Initialize journal subsystem */
void journal_init(void);

/* Begin a new transaction */
int journal_begin(struct journal_txn *txn);

/* Log a block write to the transaction */
int journal_log(struct journal_txn *txn, uint32_t block_no, const uint8_t *data);

/* Commit the transaction (write journal, then final blocks) */
int journal_commit(struct journal_txn *txn);

/* Replay committed transactions on mount (recovery) */
int journal_recover(void);

#endif /* JOURNAL_H */
