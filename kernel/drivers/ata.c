#include "ata.h"
#include "device.h"
#include "../debug/debug.h"

/* Major/minor for disk/sda */
#define ATA_MAJOR  8
#define ATA_MINOR  0

static bool primary_present = false;

/* Wait for BSY to clear */
static void ata_wait_bsy(void) {
    while (inb(ATA_PRIMARY_IO + 7) & 0x80)
        ;
}

/* Wait for DRQ to set (data ready) */
static void ata_wait_drq(void) {
    while (!(inb(ATA_PRIMARY_IO + 7) & 0x08))
        ;
}

/* Poll status after command, returns 0 on success, -1 on error */
static int ata_poll(void) {
    /* Read status 4 times to give 400ns delay */
    for (int i = 0; i < 4; i++)
        inb(ATA_PRIMARY_CTRL);

    ata_wait_bsy();

    uint8_t status = inb(ATA_PRIMARY_IO + 7);
    if (status & 0x01) return -1;  /* ERR */
    if (status & 0x20) return -1;  /* DF */
    return 0;
}

/* Block device read with offset (offset is in bytes, must be sector-aligned) */
static int ata_dev_read(struct device *dev, uint8_t *buf, uint32_t count, uint64_t offset) {
    (void)dev;
    if (!primary_present) return -1;
    if (offset % 512 != 0 || count % 512 != 0) return -1;

    uint32_t lba = (uint32_t)(offset / 512);
    uint32_t sectors = count / 512;
    if (sectors == 0 || sectors > 256) return -1;

    if (ata_read_sectors(lba, (uint8_t)(sectors & 0xFF), buf) < 0)
        return -1;
    return (int)count;
}

/* Block device write with offset (offset is in bytes, must be sector-aligned) */
static int ata_dev_write(struct device *dev, const uint8_t *buf, uint32_t count, uint64_t offset) {
    (void)dev;
    if (!primary_present) return -1;
    if (offset % 512 != 0 || count % 512 != 0) return -1;

    uint32_t lba = (uint32_t)(offset / 512);
    uint32_t sectors = count / 512;
    if (sectors == 0 || sectors > 256) return -1;

    if (ata_write_sectors(lba, (uint8_t)(sectors & 0xFF), buf) < 0)
        return -1;
    return (int)count;
}

static struct device_ops ata_dev_ops = {
    .open  = NULL,
    .close = NULL,
    .read  = ata_dev_read,
    .write = ata_dev_write,
    .ioctl = NULL,
    .mmap  = NULL,
};

void ata_init(void) {
    /* Select master drive */
    outb(ATA_PRIMARY_IO + 6, 0xA0);
    io_wait();

    /* Zero sector count and LBA ports */
    outb(ATA_PRIMARY_IO + 2, 0);
    outb(ATA_PRIMARY_IO + 3, 0);
    outb(ATA_PRIMARY_IO + 4, 0);
    outb(ATA_PRIMARY_IO + 5, 0);

    /* Send IDENTIFY */
    outb(ATA_PRIMARY_IO + 7, ATA_CMD_IDENTIFY);
    io_wait();

    uint8_t status = inb(ATA_PRIMARY_IO + 7);
    if (status == 0) {
        debug_printf("ATA: no primary disk detected\n");
        primary_present = false;
        return;
    }

    /* Wait for BSY to clear */
    ata_wait_bsy();

    /* Check LBA mid/high — if non-zero, not ATA */
    uint8_t lba_mid = inb(ATA_PRIMARY_IO + 4);
    uint8_t lba_high = inb(ATA_PRIMARY_IO + 5);
    if (lba_mid != 0 || lba_high != 0) {
        debug_printf("ATA: primary is not ATA (ATAPI or SATA?)\n");
        primary_present = false;
        return;
    }

    /* Wait for DRQ or ERR */
    while (1) {
        status = inb(ATA_PRIMARY_IO + 7);
        if (status & 0x01) {
            debug_printf("ATA: IDENTIFY returned error\n");
            primary_present = false;
            return;
        }
        if (status & 0x08) break;  /* DRQ set */
    }

    /* Read 256 words of identification data (discard) */
    for (int i = 0; i < 256; i++)
        inw(ATA_PRIMARY_IO);

    primary_present = true;
    debug_printf("ATA: primary disk detected\n");

    /* Register /dev/disk/sda as a block device */
    device_register_ex("disk/sda", DEV_BLOCK, ATA_MAJOR, ATA_MINOR, &ata_dev_ops);
}

bool ata_disk_present(void) {
    return primary_present;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    if (!primary_present)
        return -1;

    ata_wait_bsy();

    /* LBA28 mode: select master, LBA bits 24-27 */
    outb(ATA_PRIMARY_IO + 6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + 2, count);
    outb(ATA_PRIMARY_IO + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + 7, ATA_CMD_READ_SECTORS);

    uint16_t *buf = (uint16_t *)buffer;
    uint8_t sectors = count == 0 ? 0 : count;  /* 0 means 256 sectors */
    int num_sectors = sectors == 0 ? 256 : sectors;

    for (int s = 0; s < num_sectors; s++) {
        if (ata_poll() < 0)
            return -1;
        ata_wait_drq();

        /* Read 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            buf[s * 256 + i] = inw(ATA_PRIMARY_IO);
        }
    }

    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer) {
    if (!primary_present)
        return -1;

    ata_wait_bsy();

    /* LBA28 mode: select master, LBA bits 24-27 */
    outb(ATA_PRIMARY_IO + 6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + 2, count);
    outb(ATA_PRIMARY_IO + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + 7, ATA_CMD_WRITE_SECTORS);

    const uint16_t *buf = (const uint16_t *)buffer;
    uint8_t sectors = count == 0 ? 0 : count;
    int num_sectors = sectors == 0 ? 256 : sectors;

    for (int s = 0; s < num_sectors; s++) {
        if (ata_poll() < 0)
            return -1;
        ata_wait_drq();

        /* Write 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_IO, buf[s * 256 + i]);
        }

        /* Flush cache */
        outb(ATA_PRIMARY_IO + 7, 0xE7);
        ata_wait_bsy();
    }

    return 0;
}
