#ifndef EXT2_H
#define EXT2_H

#include "../common.h"

/* ext2 magic number */
#define EXT2_MAGIC 0xEF53

/* Inode types (in i_mode) */
#define EXT2_S_IFREG  0x8000   /* Regular file */
#define EXT2_S_IFDIR  0x4000   /* Directory */

/* Directory file type */
#define EXT2_FT_REG_FILE  1
#define EXT2_FT_DIR       2

/* Root inode number is always 2 */
#define EXT2_ROOT_INODE 2

struct ext2_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;      /* block_size = 1024 << s_log_block_size */
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint32_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    /* Rev 1 fields */
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
    /* ... remaining fields padded to 1024 bytes */
} __attribute__((packed));

struct ext2_block_group_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} __attribute__((packed));

struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];   /* 12 direct + 1 indirect + 1 double + 1 triple */
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} __attribute__((packed));

struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    /* name follows (variable length, padded to 4 bytes) */
} __attribute__((packed));

/* Initialize ext2 filesystem from disk. Returns 0 on success, -1 on failure. */
int ext2_init(void);

/* Read an inode by number. Returns 0 on success, -1 on failure. */
int ext2_read_inode(uint32_t ino, struct ext2_inode *out);

/* Read file data from an inode. Returns bytes read, or -1 on error. */
int ext2_read_file(struct ext2_inode *inode, uint64_t offset, uint64_t size,
                   uint8_t *buf);

/* Look up a name in a directory inode. Returns inode number, or 0 on failure. */
uint32_t ext2_lookup(uint32_t parent_ino, const char *name);

/* Look up a path starting from root. Returns inode number, or 0 on failure. */
uint32_t ext2_path_lookup(const char *path);

/* Get file data by path (reads entire file into buffer).
 * Returns bytes read, or -1 on error. */
int ext2_read_path(const char *path, uint8_t *buf, uint64_t buf_size);

/* Check if ext2 filesystem is mounted. */
bool ext2_is_mounted(void);

#endif /* EXT2_H */
