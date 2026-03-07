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
    uint8_t sb_buf[1024];
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
        uint32_t ind_buf[1024];  /* Max 4096/4 = 1024 entries */
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
        uint32_t dind_buf[1024];
        if (ext2_read_block(dind_block, dind_buf) < 0)
            return 0;

        uint32_t ind_idx = block_idx / ptrs_per_block;
        uint32_t dir_idx = block_idx % ptrs_per_block;

        uint32_t ind_block = dind_buf[ind_idx];
        if (ind_block == 0)
            return 0;
        uint32_t ind_buf[1024];
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
