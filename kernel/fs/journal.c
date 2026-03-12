#include "journal.h"
#include "../drivers/ata.h"
#include "../string.h"
#include "../debug/debug.h"

static uint32_t journal_seq = 0;
static bool journal_ready = false;

/* Journal area layout (in sectors, at JOURNAL_START_LBA):
 * [header sector(s)] [block data...]
 * Each transaction: 1 sector header + N*8 sectors of block data (4KB blocks) */

void journal_init(void) {
    journal_seq = 0;
    journal_ready = true;
    debug_printf("journal: initialized at LBA %u\n", (uint64_t)JOURNAL_START_LBA);
}

int journal_begin(struct journal_txn *txn) {
    if (!txn || !journal_ready)
        return -1;
    memset(txn, 0, sizeof(*txn));
    txn->active = true;
    return 0;
}

int journal_log(struct journal_txn *txn, uint32_t block_no, const uint8_t *data) {
    if (!txn || !txn->active)
        return -1;
    if (txn->count >= JOURNAL_MAX_BLOCKS)
        return -1;

    txn->target_blocks[txn->count] = block_no;
    txn->block_data[txn->count] = (uint8_t *)data;
    txn->count++;
    return 0;
}

int journal_commit(struct journal_txn *txn) {
    if (!txn || !txn->active || txn->count == 0)
        return -1;

    /* Step 1: Write journal header (uncommitted) */
    static uint8_t hdr_buf[512];
    memset(hdr_buf, 0, sizeof(hdr_buf));
    struct journal_header *hdr = (struct journal_header *)hdr_buf;
    hdr->magic = JOURNAL_MAGIC;
    hdr->seq = journal_seq;
    hdr->block_count = (uint32_t)txn->count;
    hdr->committed = 0;  /* Not yet committed */
    for (int i = 0; i < txn->count; i++)
        hdr->target_blocks[i] = txn->target_blocks[i];

    uint32_t jrnl_lba = JOURNAL_START_LBA;
    ata_write_sectors(jrnl_lba, 1, hdr_buf);
    jrnl_lba += 1;

    /* Step 2: Write block data to journal area */
    for (int i = 0; i < txn->count; i++) {
        /* Each 4KB block = 8 sectors */
        ata_write_sectors(jrnl_lba, 8, txn->block_data[i]);
        jrnl_lba += 8;
    }

    /* Step 3: Mark committed */
    hdr->committed = 1;
    ata_write_sectors(JOURNAL_START_LBA, 1, hdr_buf);

    /* Step 4: Write blocks to final locations */
    for (int i = 0; i < txn->count; i++) {
        uint32_t sectors_per_block = 8;  /* 4096/512 */
        uint32_t final_lba = txn->target_blocks[i] * sectors_per_block;
        ata_write_sectors(final_lba, (uint8_t)sectors_per_block, txn->block_data[i]);
    }

    /* Step 5: Clear journal (mark as applied) */
    hdr->committed = 2;  /* 2 = applied */
    ata_write_sectors(JOURNAL_START_LBA, 1, hdr_buf);

    journal_seq++;
    txn->active = false;
    return 0;
}

int journal_recover(void) {
    if (!journal_ready)
        return -1;

    static uint8_t hdr_buf[512];
    if (ata_read_sectors(JOURNAL_START_LBA, 1, hdr_buf) < 0)
        return -1;

    struct journal_header *hdr = (struct journal_header *)hdr_buf;
    if (hdr->magic != JOURNAL_MAGIC)
        return 0;  /* No journal to recover */

    if (hdr->committed == 1) {
        /* Committed but not applied — replay */
        debug_printf("journal: recovering transaction seq=%u with %u blocks\n",
                     (uint64_t)hdr->seq, (uint64_t)hdr->block_count);

        uint32_t jrnl_lba = JOURNAL_START_LBA + 1;
        static uint8_t block_data[4096];

        for (uint32_t i = 0; i < hdr->block_count && i < JOURNAL_MAX_BLOCKS; i++) {
            if (ata_read_sectors(jrnl_lba, 8, block_data) < 0) {
                debug_printf("journal: recovery read failed at block %u\n", (uint64_t)i);
                return -1;
            }

            uint32_t final_lba = hdr->target_blocks[i] * 8;
            ata_write_sectors(final_lba, 8, block_data);
            jrnl_lba += 8;
        }

        /* Mark as applied */
        hdr->committed = 2;
        ata_write_sectors(JOURNAL_START_LBA, 1, hdr_buf);

        debug_printf("journal: recovery complete\n");
        return 1;  /* Recovered */
    }

    return 0;  /* Nothing to recover */
}
