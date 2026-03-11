#include "ext2.h"
#include "../drivers/ata.h"
#include "../memory/kheap.h"
#include "../string.h"
#include "../debug/debug.h"

static bool ext2_mounted = false;
static struct ext2_superblock sb;
static struct ext2_block_group_desc *bgdt = NULL;
static uint32_t block_size = 1024;
static uint32_t num_block_groups = 0;
static uint32_t inode_size = 128;

/* Scratch buffer for disk I/O (1 block, max 4096 bytes) */
static uint8_t block_buf[4096];

/* Scratch buffer for indirect block lookups (avoids 4KB stack allocations) */
static uint32_t ind_buf[1024];

/* Read a filesystem block into buffer */
static int ext2_read_block(uint32_t block_no, void *buf) {
    uint32_t sectors_per_block = block_size / 512;
    uint32_t lba = block_no * sectors_per_block;
    return ata_read_sectors(lba, (uint8_t)sectors_per_block, buf);
}

int ext2_init(void) {
    if (!ata_disk_present()) {
        debug_printf("ext2: no disk present\n");
        return -1;
    }

    /* Read superblock (at byte offset 1024 = LBA 2) */
    static uint8_t sb_buf[1024];
    if (ata_read_sectors(2, 2, sb_buf) < 0) {
        debug_printf("ext2: failed to read superblock\n");
        return -1;
    }
    memcpy(&sb, sb_buf, sizeof(struct ext2_superblock));

    /* Verify magic */
    if (sb.s_magic != EXT2_MAGIC) {
        debug_printf("ext2: bad magic 0x%x (expected 0xEF53)\n",
                     (uint64_t)sb.s_magic);
        return -1;
    }

    block_size = 1024u << sb.s_log_block_size;
    inode_size = (sb.s_rev_level >= 1) ? sb.s_inode_size : 128;
    num_block_groups = (sb.s_blocks_count + sb.s_blocks_per_group - 1) /
                       sb.s_blocks_per_group;

    debug_printf("ext2: mounted: %u blocks, %u inodes, block_size=%u\n",
                 (uint64_t)sb.s_blocks_count, (uint64_t)sb.s_inodes_count,
                 (uint64_t)block_size);

    if (block_size > 4096) {
        debug_printf("ext2: block size %u too large\n", (uint64_t)block_size);
        return -1;
    }

    /* Read block group descriptor table (starts at block after superblock) */
    uint32_t bgdt_block = (block_size == 1024) ? 2 : 1;
    uint32_t bgdt_size = num_block_groups * sizeof(struct ext2_block_group_desc);
    bgdt = (struct ext2_block_group_desc *)kmalloc(bgdt_size);
    if (!bgdt)
        return -1;

    if (ext2_read_block(bgdt_block, block_buf) < 0) {
        debug_printf("ext2: failed to read BGDT\n");
        return -1;
    }
    memcpy(bgdt, block_buf, bgdt_size < block_size ? bgdt_size : block_size);

    ext2_mounted = true;
    debug_printf("ext2: filesystem ready (%u block groups)\n",
                 (uint64_t)num_block_groups);
    return 0;
}

bool ext2_is_mounted(void) {
    return ext2_mounted;
}

int ext2_read_inode(uint32_t ino, struct ext2_inode *out) {
    if (!ext2_mounted || ino == 0)
        return -1;

    /* Determine which block group the inode is in */
    uint32_t bg = (ino - 1) / sb.s_inodes_per_group;
    uint32_t idx = (ino - 1) % sb.s_inodes_per_group;

    /* Find inode in the inode table */
    uint32_t inode_table_block = bgdt[bg].bg_inode_table;
    uint32_t offset_in_table = idx * inode_size;
    uint32_t block_containing = inode_table_block + (offset_in_table / block_size);
    uint32_t offset_in_block = offset_in_table % block_size;

    if (ext2_read_block(block_containing, block_buf) < 0)
        return -1;

    memcpy(out, block_buf + offset_in_block, sizeof(struct ext2_inode));
    return 0;
}

/* Resolve a block index for an inode (handles indirect blocks) */
static uint32_t ext2_get_block(struct ext2_inode *inode, uint32_t block_idx) {
    uint32_t ptrs_per_block = block_size / 4;

    /* Direct blocks (0-11) */
    if (block_idx < 12)
        return inode->i_block[block_idx];

    /* Singly indirect (12 to 12+ptrs_per_block-1) */
    block_idx -= 12;
    if (block_idx < ptrs_per_block) {
        uint32_t indirect_block = inode->i_block[12];
        if (indirect_block == 0)
            return 0;
        if (ext2_read_block(indirect_block, ind_buf) < 0)
            return 0;
        return ind_buf[block_idx];
    }

    /* Doubly indirect */
    block_idx -= ptrs_per_block;
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        uint32_t dind_block = inode->i_block[13];
        if (dind_block == 0)
            return 0;
        if (ext2_read_block(dind_block, ind_buf) < 0)
            return 0;

        uint32_t ind_idx = block_idx / ptrs_per_block;
        uint32_t dir_idx = block_idx % ptrs_per_block;

        uint32_t ind_block = ind_buf[ind_idx];
        if (ind_block == 0)
            return 0;
        if (ext2_read_block(ind_block, ind_buf) < 0)
            return 0;
        return ind_buf[dir_idx];
    }

    /* Triply indirect — unlikely for 16MB disk */
    return 0;
}

int ext2_read_file(struct ext2_inode *inode, uint64_t offset, uint64_t size,
                   uint8_t *buf) {
    if (!inode || !buf)
        return -1;

    uint32_t file_size = inode->i_size;
    if (offset >= file_size)
        return 0;

    uint64_t remaining = file_size - offset;
    if (size > remaining)
        size = remaining;

    uint64_t bytes_read = 0;
    while (bytes_read < size) {
        uint32_t block_idx = (uint32_t)((offset + bytes_read) / block_size);
        uint32_t offset_in_block = (uint32_t)((offset + bytes_read) % block_size);
        uint32_t to_read = block_size - offset_in_block;
        if (to_read > size - bytes_read)
            to_read = (uint32_t)(size - bytes_read);

        uint32_t disk_block = ext2_get_block(inode, block_idx);
        if (disk_block == 0) {
            /* Sparse block: fill with zeros */
            memset(buf + bytes_read, 0, to_read);
        } else {
            if (ext2_read_block(disk_block, block_buf) < 0)
                return -1;
            memcpy(buf + bytes_read, block_buf + offset_in_block, to_read);
        }

        bytes_read += to_read;
    }

    return (int)bytes_read;
}

uint32_t ext2_lookup(uint32_t parent_ino, const char *name) {
    struct ext2_inode dir_inode;
    if (ext2_read_inode(parent_ino, &dir_inode) < 0)
        return 0;

    if (!(dir_inode.i_mode & EXT2_S_IFDIR))
        return 0;

    uint32_t dir_size = dir_inode.i_size;
    uint32_t offset = 0;
    size_t name_len = strlen(name);

    while (offset < dir_size) {
        uint32_t block_idx = offset / block_size;
        uint32_t block_offset = offset % block_size;

        uint32_t disk_block = ext2_get_block(&dir_inode, block_idx);
        if (disk_block == 0)
            break;

        if (ext2_read_block(disk_block, block_buf) < 0)
            break;

        /* Process directory entries in this block */
        while (block_offset < block_size && offset < dir_size) {
            struct ext2_dir_entry *de =
                (struct ext2_dir_entry *)(block_buf + block_offset);

            if (de->rec_len == 0)
                break;

            if (de->inode != 0 && de->name_len == name_len) {
                char *de_name = (char *)((uint8_t *)de +
                                sizeof(struct ext2_dir_entry));
                if (strncmp(de_name, name, name_len) == 0)
                    return de->inode;
            }

            offset += de->rec_len;
            block_offset += de->rec_len;
        }
    }

    return 0;
}

uint32_t ext2_path_lookup(const char *path) {
    if (!ext2_mounted || !path)
        return 0;

    /* Skip leading slash */
    if (*path == '/')
        path++;
    if (*path == '\0')
        return EXT2_ROOT_INODE;

    uint32_t cur_ino = EXT2_ROOT_INODE;

    /* Tokenize path and look up each component */
    char pathbuf[256];
    strncpy(pathbuf, path, sizeof(pathbuf) - 1);
    pathbuf[sizeof(pathbuf) - 1] = '\0';

    char *component = pathbuf;
    while (*component) {
        /* Find end of component */
        char *next = component;
        while (*next && *next != '/')
            next++;

        bool has_more = (*next == '/');
        if (has_more)
            *next = '\0';

        if (*component == '\0')
            break;

        cur_ino = ext2_lookup(cur_ino, component);
        if (cur_ino == 0)
            return 0;

        if (has_more)
            component = next + 1;
        else
            break;
    }

    return cur_ino;
}

int ext2_read_path(const char *path, uint8_t *buf, uint64_t buf_size) {
    uint32_t ino = ext2_path_lookup(path);
    if (ino == 0)
        return -1;

    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0)
        return -1;

    return ext2_read_file(&inode, 0, buf_size, buf);
}

/* ---- Writable ext2 support ---- */

/* Write a filesystem block to disk */
static int ext2_write_block(uint32_t block_no, const void *buf) {
    uint32_t sectors_per_block = block_size / 512;
    uint32_t lba = block_no * sectors_per_block;
    return ata_write_sectors(lba, (uint8_t)sectors_per_block, buf);
}

/* Allocate a free block from the block bitmap */
static uint32_t ext2_alloc_block(void) {
    for (uint32_t bg = 0; bg < num_block_groups; bg++) {
        if (bgdt[bg].bg_free_blocks_count == 0)
            continue;

        /* Read block bitmap */
        if (ext2_read_block(bgdt[bg].bg_block_bitmap, block_buf) < 0)
            continue;

        for (uint32_t i = 0; i < sb.s_blocks_per_group; i++) {
            uint32_t byte = i / 8;
            uint32_t bit = i % 8;
            if (!(block_buf[byte] & (1 << bit))) {
                /* Found free block, mark used */
                block_buf[byte] |= (1 << bit);
                ext2_write_block(bgdt[bg].bg_block_bitmap, block_buf);

                bgdt[bg].bg_free_blocks_count--;
                sb.s_free_blocks_count--;

                uint32_t block_no = bg * sb.s_blocks_per_group +
                                    i + sb.s_first_data_block;
                debug_printf("ext2: allocated block %u\n",
                             (uint64_t)block_no);
                return block_no;
            }
        }
    }
    debug_printf("ext2: no free blocks\n");
    return 0;
}

/* Free a block back to the block bitmap */
static void ext2_free_block(uint32_t block_no) {
    if (block_no == 0)
        return;

    uint32_t adjusted = block_no - sb.s_first_data_block;
    uint32_t bg = adjusted / sb.s_blocks_per_group;
    uint32_t idx = adjusted % sb.s_blocks_per_group;

    if (bg >= num_block_groups)
        return;

    if (ext2_read_block(bgdt[bg].bg_block_bitmap, block_buf) < 0)
        return;

    uint32_t byte = idx / 8;
    uint32_t bit = idx % 8;
    block_buf[byte] &= ~(1 << bit);
    ext2_write_block(bgdt[bg].bg_block_bitmap, block_buf);

    bgdt[bg].bg_free_blocks_count++;
    sb.s_free_blocks_count++;
}

/* Allocate a free inode from the inode bitmap */
static uint32_t ext2_alloc_inode(void) {
    for (uint32_t bg = 0; bg < num_block_groups; bg++) {
        if (bgdt[bg].bg_free_inodes_count == 0)
            continue;

        if (ext2_read_block(bgdt[bg].bg_inode_bitmap, block_buf) < 0)
            continue;

        for (uint32_t i = 0; i < sb.s_inodes_per_group; i++) {
            uint32_t byte = i / 8;
            uint32_t bit = i % 8;
            if (!(block_buf[byte] & (1 << bit))) {
                block_buf[byte] |= (1 << bit);
                ext2_write_block(bgdt[bg].bg_inode_bitmap, block_buf);

                bgdt[bg].bg_free_inodes_count--;
                sb.s_free_inodes_count--;

                uint32_t ino = bg * sb.s_inodes_per_group + i + 1;
                debug_printf("ext2: allocated inode %u\n", (uint64_t)ino);
                return ino;
            }
        }
    }
    debug_printf("ext2: no free inodes\n");
    return 0;
}

/* Free an inode back to the inode bitmap */
static void ext2_free_inode(uint32_t ino) {
    if (ino == 0)
        return;

    uint32_t bg = (ino - 1) / sb.s_inodes_per_group;
    uint32_t idx = (ino - 1) % sb.s_inodes_per_group;

    if (bg >= num_block_groups)
        return;

    if (ext2_read_block(bgdt[bg].bg_inode_bitmap, block_buf) < 0)
        return;

    uint32_t byte = idx / 8;
    uint32_t bit = idx % 8;
    block_buf[byte] &= ~(1 << bit);
    ext2_write_block(bgdt[bg].bg_inode_bitmap, block_buf);

    bgdt[bg].bg_free_inodes_count++;
    sb.s_free_inodes_count++;
}

/* Write an inode struct back to disk */
int ext2_write_inode(uint32_t ino, const struct ext2_inode *inode) {
    if (!ext2_mounted || ino == 0)
        return -1;

    uint32_t bg = (ino - 1) / sb.s_inodes_per_group;
    uint32_t idx = (ino - 1) % sb.s_inodes_per_group;
    uint32_t inode_table_block = bgdt[bg].bg_inode_table;
    uint32_t offset_in_table = idx * inode_size;
    uint32_t block_containing = inode_table_block + (offset_in_table / block_size);
    uint32_t offset_in_block = offset_in_table % block_size;

    if (ext2_read_block(block_containing, block_buf) < 0)
        return -1;

    memcpy(block_buf + offset_in_block, inode, sizeof(struct ext2_inode));
    return ext2_write_block(block_containing, block_buf);
}

/* Flush superblock and BGDT to disk */
static void ext2_flush_metadata(void) {
    /* Write superblock at LBA 2 */
    static uint8_t sb_buf[1024];
    memset(sb_buf, 0, sizeof(sb_buf));
    memcpy(sb_buf, &sb, sizeof(struct ext2_superblock));
    ata_write_sectors(2, 2, sb_buf);

    /* Write BGDT */
    uint32_t bgdt_block = (block_size == 1024) ? 2 : 1;
    uint32_t bgdt_size = num_block_groups * sizeof(struct ext2_block_group_desc);

    if (ext2_read_block(bgdt_block, block_buf) < 0)
        return;
    memcpy(block_buf, bgdt, bgdt_size < block_size ? bgdt_size : block_size);
    ext2_write_block(bgdt_block, block_buf);
}

/* Assign a disk block to a specific block index of an inode */
static int ext2_set_block(struct ext2_inode *inode, uint32_t block_idx,
                          uint32_t disk_block) {
    uint32_t ptrs_per_block = block_size / 4;

    if (block_idx < 12) {
        inode->i_block[block_idx] = disk_block;
        return 0;
    }

    block_idx -= 12;
    if (block_idx < ptrs_per_block) {
        /* Singly indirect */
        if (inode->i_block[12] == 0) {
            uint32_t ind = ext2_alloc_block();
            if (!ind) return -1;
            inode->i_block[12] = ind;
            /* Zero out new indirect block */
            memset(block_buf, 0, block_size);
            ext2_write_block(ind, block_buf);
        }
        if (ext2_read_block(inode->i_block[12], ind_buf) < 0)
            return -1;
        ind_buf[block_idx] = disk_block;
        return ext2_write_block(inode->i_block[12], ind_buf);
    }

    /* Doubly indirect not implemented for 16MB disk */
    return -1;
}

/* Write data to a file, allocating blocks as needed */
int ext2_write_file(uint32_t ino, struct ext2_inode *inode, uint64_t offset,
                    uint64_t size, const uint8_t *buf) {
    if (!ext2_mounted || !inode || !buf)
        return -1;

    uint64_t bytes_written = 0;
    while (bytes_written < size) {
        uint32_t block_idx = (uint32_t)((offset + bytes_written) / block_size);
        uint32_t off_in_block = (uint32_t)((offset + bytes_written) % block_size);
        uint32_t to_write = block_size - off_in_block;
        if (to_write > size - bytes_written)
            to_write = (uint32_t)(size - bytes_written);

        uint32_t disk_block = ext2_get_block(inode, block_idx);
        if (disk_block == 0) {
            /* Allocate new block */
            disk_block = ext2_alloc_block();
            if (!disk_block)
                return (bytes_written > 0) ? (int)bytes_written : -1;
            ext2_set_block(inode, block_idx, disk_block);
            inode->i_blocks += block_size / 512;
            /* Zero out new block */
            memset(block_buf, 0, block_size);
            ext2_write_block(disk_block, block_buf);
        }

        /* Read-modify-write if partial block */
        if (off_in_block != 0 || to_write != block_size) {
            if (ext2_read_block(disk_block, block_buf) < 0)
                return (bytes_written > 0) ? (int)bytes_written : -1;
        }

        memcpy(block_buf + off_in_block, buf + bytes_written, to_write);
        if (ext2_write_block(disk_block, block_buf) < 0)
            return (bytes_written > 0) ? (int)bytes_written : -1;

        bytes_written += to_write;
    }

    /* Update file size if we extended it */
    uint64_t new_end = offset + bytes_written;
    if (new_end > inode->i_size)
        inode->i_size = (uint32_t)new_end;

    /* Write back inode */
    ext2_write_inode(ino, inode);
    ext2_flush_metadata();

    return (int)bytes_written;
}

/* Add a directory entry to a directory */
static int ext2_add_dir_entry(uint32_t dir_ino, uint32_t ino,
                               const char *name, uint8_t file_type) {
    struct ext2_inode dir_inode;
    if (ext2_read_inode(dir_ino, &dir_inode) < 0)
        return -1;

    uint32_t name_len = (uint32_t)strlen(name);
    /* Required size for new entry: header + name, aligned to 4 bytes */
    uint32_t needed = sizeof(struct ext2_dir_entry) + name_len;
    needed = (needed + 3) & ~3u;

    uint32_t dir_size = dir_inode.i_size;
    uint32_t offset = 0;

    while (offset < dir_size) {
        uint32_t block_idx = offset / block_size;
        uint32_t disk_block = ext2_get_block(&dir_inode, block_idx);
        if (disk_block == 0)
            break;

        if (ext2_read_block(disk_block, block_buf) < 0)
            return -1;

        uint32_t block_offset = offset % block_size;
        while (block_offset < block_size && offset < dir_size) {
            struct ext2_dir_entry *de =
                (struct ext2_dir_entry *)(block_buf + block_offset);

            if (de->rec_len == 0)
                break;

            /* Check if this entry has room at the end */
            uint32_t actual = sizeof(struct ext2_dir_entry) + de->name_len;
            actual = (actual + 3) & ~3u;
            uint32_t free_space = de->rec_len - actual;

            if (free_space >= needed) {
                /* Split this entry */
                uint16_t old_rec_len = de->rec_len;
                de->rec_len = (uint16_t)actual;

                /* Create new entry after this one */
                struct ext2_dir_entry *new_de =
                    (struct ext2_dir_entry *)(block_buf + block_offset + actual);
                new_de->inode = ino;
                new_de->rec_len = (uint16_t)(old_rec_len - actual);
                new_de->name_len = (uint8_t)name_len;
                new_de->file_type = file_type;
                memcpy((uint8_t *)new_de + sizeof(struct ext2_dir_entry),
                       name, name_len);

                ext2_write_block(disk_block, block_buf);
                return 0;
            }

            offset += de->rec_len;
            block_offset += de->rec_len;
        }
    }

    /* No room in existing blocks — allocate a new block */
    uint32_t new_block = ext2_alloc_block();
    if (!new_block)
        return -1;

    uint32_t block_idx = dir_size / block_size;
    ext2_set_block(&dir_inode, block_idx, new_block);
    dir_inode.i_blocks += block_size / 512;
    dir_inode.i_size += block_size;

    /* Initialize new block with single entry spanning entire block */
    memset(block_buf, 0, block_size);
    struct ext2_dir_entry *de = (struct ext2_dir_entry *)block_buf;
    de->inode = ino;
    de->rec_len = (uint16_t)block_size;
    de->name_len = (uint8_t)name_len;
    de->file_type = file_type;
    memcpy(block_buf + sizeof(struct ext2_dir_entry), name, name_len);

    ext2_write_block(new_block, block_buf);
    ext2_write_inode(dir_ino, &dir_inode);
    ext2_flush_metadata();
    return 0;
}

/* Remove a directory entry by name */
static int ext2_remove_dir_entry(uint32_t dir_ino, const char *name) {
    struct ext2_inode dir_inode;
    if (ext2_read_inode(dir_ino, &dir_inode) < 0)
        return -1;

    uint32_t name_len = (uint32_t)strlen(name);
    uint32_t dir_size = dir_inode.i_size;
    uint32_t offset = 0;

    while (offset < dir_size) {
        uint32_t block_idx = offset / block_size;
        uint32_t disk_block = ext2_get_block(&dir_inode, block_idx);
        if (disk_block == 0)
            break;

        if (ext2_read_block(disk_block, block_buf) < 0)
            return -1;

        uint32_t block_offset = 0;
        struct ext2_dir_entry *prev = NULL;

        while (block_offset < block_size) {
            struct ext2_dir_entry *de =
                (struct ext2_dir_entry *)(block_buf + block_offset);

            if (de->rec_len == 0)
                break;

            if (de->inode != 0 && de->name_len == name_len) {
                char *de_name = (char *)((uint8_t *)de +
                                sizeof(struct ext2_dir_entry));
                if (strncmp(de_name, name, name_len) == 0) {
                    /* Found it — merge with previous entry or zero inode */
                    if (prev) {
                        prev->rec_len += de->rec_len;
                    } else {
                        de->inode = 0;
                    }
                    ext2_write_block(disk_block, block_buf);
                    return 0;
                }
            }

            prev = de;
            block_offset += de->rec_len;
        }
        offset += block_size;
    }

    return -1;  /* Not found */
}

/* Create a new regular file */
uint32_t ext2_create(uint32_t parent_ino, const char *name, uint16_t mode) {
    if (!ext2_mounted)
        return 0;

    /* Check if name already exists */
    if (ext2_lookup(parent_ino, name) != 0)
        return 0;

    /* Allocate inode */
    uint32_t ino = ext2_alloc_inode();
    if (!ino)
        return 0;

    /* Initialize inode */
    struct ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = mode;
    inode.i_links_count = 1;

    if (ext2_write_inode(ino, &inode) < 0) {
        ext2_free_inode(ino);
        return 0;
    }

    /* Add directory entry */
    uint8_t ft = (mode & EXT2_S_IFDIR) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    if (ext2_add_dir_entry(parent_ino, ino, name, ft) < 0) {
        ext2_free_inode(ino);
        return 0;
    }

    ext2_flush_metadata();
    debug_printf("ext2: created '%s' inode=%u\n", name, (uint64_t)ino);
    return ino;
}

/* Create a new directory with . and .. entries */
uint32_t ext2_mkdir(uint32_t parent_ino, const char *name) {
    if (!ext2_mounted) {
        debug_printf("ext2: mkdir '%s' failed: not mounted\n", name);
        return 0;
    }

    if (ext2_lookup(parent_ino, name) != 0) {
        debug_printf("ext2: mkdir '%s' failed: already exists\n", name);
        return 0;
    }

    /* Allocate inode */
    uint32_t ino = ext2_alloc_inode();
    if (!ino) {
        debug_printf("ext2: mkdir '%s' failed: no free inodes\n", name);
        return 0;
    }

    /* Allocate a block for . and .. entries */
    uint32_t dir_block = ext2_alloc_block();
    if (!dir_block) {
        debug_printf("ext2: mkdir '%s' failed: no free blocks\n", name);
        ext2_free_inode(ino);
        return 0;
    }

    /* Initialize inode */
    struct ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = EXT2_S_IFDIR | 0755;
    inode.i_links_count = 2;  /* . and parent's entry */
    inode.i_size = block_size;
    inode.i_blocks = block_size / 512;
    inode.i_block[0] = dir_block;

    /* Create . and .. entries */
    memset(block_buf, 0, block_size);

    /* . entry */
    struct ext2_dir_entry *dot = (struct ext2_dir_entry *)block_buf;
    dot->inode = ino;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    block_buf[sizeof(struct ext2_dir_entry)] = '.';

    /* .. entry */
    struct ext2_dir_entry *dotdot = (struct ext2_dir_entry *)(block_buf + 12);
    dotdot->inode = parent_ino;
    dotdot->rec_len = (uint16_t)(block_size - 12);
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    block_buf[12 + sizeof(struct ext2_dir_entry)] = '.';
    block_buf[12 + sizeof(struct ext2_dir_entry) + 1] = '.';

    ext2_write_block(dir_block, block_buf);
    ext2_write_inode(ino, &inode);

    /* Add entry to parent directory */
    if (ext2_add_dir_entry(parent_ino, ino, name, EXT2_FT_DIR) < 0) {
        ext2_free_block(dir_block);
        ext2_free_inode(ino);
        return 0;
    }

    /* Increment parent's link count (for ..) */
    struct ext2_inode parent_inode;
    if (ext2_read_inode(parent_ino, &parent_inode) == 0) {
        parent_inode.i_links_count++;
        ext2_write_inode(parent_ino, &parent_inode);
    }

    /* Update used_dirs_count */
    uint32_t bg = (ino - 1) / sb.s_inodes_per_group;
    bgdt[bg].bg_used_dirs_count++;

    ext2_flush_metadata();
    debug_printf("ext2: mkdir '%s' inode=%u\n", name, (uint64_t)ino);
    return ino;
}

/* Unlink a file: remove dir entry, free blocks/inode if links=0 */
int ext2_unlink(uint32_t parent_ino, const char *name) {
    if (!ext2_mounted)
        return -1;

    /* Look up inode */
    uint32_t ino = ext2_lookup(parent_ino, name);
    if (!ino)
        return -1;

    /* Remove directory entry */
    if (ext2_remove_dir_entry(parent_ino, name) < 0)
        return -1;

    /* Decrement link count */
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0)
        return -1;

    inode.i_links_count--;
    if (inode.i_links_count == 0) {
        /* Free all data blocks */
        uint32_t num_blocks = (inode.i_size + block_size - 1) / block_size;
        for (uint32_t i = 0; i < num_blocks && i < 12; i++) {
            if (inode.i_block[i])
                ext2_free_block(inode.i_block[i]);
        }
        /* Free indirect block if present */
        if (inode.i_block[12]) {
            if (ext2_read_block(inode.i_block[12], ind_buf) == 0) {
                uint32_t ptrs = block_size / 4;
                for (uint32_t i = 0; i < ptrs; i++) {
                    if (ind_buf[i])
                        ext2_free_block(ind_buf[i]);
                }
            }
            ext2_free_block(inode.i_block[12]);
        }

        /* Free inode */
        ext2_free_inode(ino);
        memset(&inode, 0, sizeof(inode));
    }

    ext2_write_inode(ino, &inode);
    ext2_flush_metadata();
    debug_printf("ext2: unlinked '%s' inode=%u\n", name, (uint64_t)ino);
    return 0;
}

/* Split path into parent directory and basename */
static void ext2_path_split(const char *path, char *parent, char *name) {
    int len = 0;
    while (path[len]) len++;
    int slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') { slash = i; break; }
    }
    if (slash <= 0) {
        parent[0] = '/'; parent[1] = '\0';
        strncpy(name, path + (slash + 1), 127);
        name[127] = '\0';
    } else {
        int plen = slash < 127 ? slash : 127;
        strncpy(parent, path, (size_t)plen);
        parent[plen] = '\0';
        strncpy(name, path + slash + 1, 127);
        name[127] = '\0';
    }
}

int ext2_rename(const char *old_path, const char *new_path) {
    if (!ext2_mounted) return -1;

    uint32_t old_ino = ext2_path_lookup(old_path);
    if (!old_ino) return -1;

    char old_parent[128], old_name[128];
    char new_parent[128], new_name[128];
    ext2_path_split(old_path, old_parent, old_name);
    ext2_path_split(new_path, new_parent, new_name);

    uint32_t old_dir_ino = ext2_path_lookup(old_parent);
    uint32_t new_dir_ino = ext2_path_lookup(new_parent);
    if (!old_dir_ino || !new_dir_ino) return -1;

    struct ext2_inode old_inode;
    if (ext2_read_inode(old_ino, &old_inode) != 0) return -1;
    uint8_t ftype = (old_inode.i_mode & EXT2_S_IFDIR) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;

    /* Remove existing destination if present */
    uint32_t dst_ino = ext2_lookup(new_dir_ino, new_name);
    if (dst_ino) {
        ext2_unlink(new_dir_ino, new_name);
    }

    /* Add entry in new location pointing to old inode */
    if (ext2_add_dir_entry(new_dir_ino, old_ino, new_name, ftype) < 0)
        return -1;

    /* Remove old directory entry */
    if (ext2_remove_dir_entry(old_dir_ino, old_name) < 0) {
        ext2_remove_dir_entry(new_dir_ino, new_name);
        return -1;
    }

    ext2_flush_metadata();
    debug_printf("ext2: renamed '%s' -> '%s'\n", old_path, new_path);
    return 0;
}

int ext2_get_stats(struct ext2_stats *stats) {
    if (!ext2_mounted || !stats) return -1;
    stats->total_blocks = sb.s_blocks_count;
    stats->free_blocks  = sb.s_free_blocks_count;
    stats->total_inodes = sb.s_inodes_count;
    stats->free_inodes  = sb.s_free_inodes_count;
    stats->block_size   = block_size;
    return 0;
}
