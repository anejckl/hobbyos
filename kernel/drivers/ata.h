#ifndef ATA_H
#define ATA_H

#include "../common.h"

/* ATA PIO port addresses */
#define ATA_PRIMARY_IO    0x1F0
#define ATA_PRIMARY_CTRL  0x3F6

/* ATA commands */
#define ATA_CMD_READ_SECTORS   0x20
#define ATA_CMD_WRITE_SECTORS  0x30
#define ATA_CMD_IDENTIFY       0xEC

/* Initialize ATA driver and detect disks. */
void ata_init(void);

/* Read sectors from primary disk using PIO.
 * lba - starting logical block address
 * count - number of sectors to read (max 256, 0 means 256)
 * buffer - destination buffer (must be count*512 bytes)
 * Returns 0 on success, -1 on error. */
int ata_read_sectors(uint32_t lba, uint8_t count, void *buffer);

/* Write sectors to primary disk using PIO.
 * lba - starting logical block address
 * count - number of sectors to write (max 256, 0 means 256)
 * buffer - source buffer (must be count*512 bytes)
 * Returns 0 on success, -1 on error. */
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer);

/* Returns true if primary disk is present. */
bool ata_disk_present(void);

#endif /* ATA_H */
