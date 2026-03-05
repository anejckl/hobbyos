/*
 * test_vfs.c — Unit tests for VFS + RAMFS.
 *
 * Includes vfs.c and ramfs.c directly. String functions are provided by
 * test_string.c (which includes kernel/string.c) at link time.
 */

/* Block kernel headers FIRST */
#define COMMON_H
#define STRING_H
#define DEBUG_H

#include "stubs.h"
#include "test_main.h"
#include <string.h>

/* Rename debug_printf to avoid conflict with test_pmm.c */
#define debug_printf vfs_test_debug_printf
static void vfs_test_debug_printf(const char *fmt, ...) { (void)fmt; }

/* Declare kernel string functions (defined in test_string.c's compilation unit) */
extern size_t strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, size_t n);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);

/* Include VFS and RAMFS source directly */
#include "../kernel/fs/vfs.c"
#include "../kernel/fs/ramfs.c"

#undef debug_printf

static void test_vfs_init_ok(void) {
    vfs_init();
    TEST("vfs_init succeeds", 1);
}

static void test_vfs_lookup_root(void) {
    vfs_init();
    struct vfs_node *node = vfs_lookup("/");
    TEST("lookup '/' returns root", node != NULL);
    TEST("root is directory", node != NULL && node->type == VFS_DIRECTORY);
}

static void test_vfs_register_and_lookup(void) {
    vfs_init();
    struct vfs_node *node = vfs_register_node("testfile", VFS_FILE);
    TEST("register returns non-NULL", node != NULL);

    struct vfs_node *found = vfs_lookup("/testfile");
    TEST("lookup '/testfile' finds it", found == node);

    struct vfs_node *found2 = vfs_lookup("testfile");
    TEST("lookup 'testfile' also finds it", found2 == node);
}

static void test_vfs_lookup_missing(void) {
    vfs_init();
    struct vfs_node *node = vfs_lookup("/nonexistent");
    TEST("lookup nonexistent returns NULL", node == NULL);
}

static void test_vfs_open_close(void) {
    vfs_init();
    vfs_register_node("opentest", VFS_FILE);

    int fd = vfs_open("/opentest");
    TEST("open returns fd >= 3", fd >= 3);

    int ret = vfs_close(fd);
    TEST("close returns 0", ret == 0);

    ret = vfs_close(fd);
    TEST("double close returns -1", ret == -1);
}

static void test_vfs_open_missing(void) {
    vfs_init();
    int fd = vfs_open("/nosuchfile");
    TEST("open nonexistent returns -1", fd == -1);
}

static void test_vfs_close_reserved(void) {
    vfs_init();
    TEST("close fd 0 fails", vfs_close(0) == -1);
    TEST("close fd 1 fails", vfs_close(1) == -1);
    TEST("close fd 2 fails", vfs_close(2) == -1);
}

static void test_ramfs_add_and_read(void) {
    vfs_init();
    ramfs_init();

    const uint8_t data[] = "Hello, RAMFS!";
    int ret = ramfs_add_file("greeting", data, sizeof(data) - 1);
    TEST("ramfs_add_file returns 0", ret == 0);

    int fd = vfs_open("/greeting");
    TEST("open ramfs file succeeds", fd >= 3);

    uint8_t buf[64] = {0};
    int bytes = vfs_read(fd, buf, sizeof(buf));
    TEST("read returns 13 bytes", bytes == 13);
    TEST("read data matches", memcmp(buf, "Hello, RAMFS!", 13) == 0);

    bytes = vfs_read(fd, buf, sizeof(buf));
    TEST("second read returns 0 (EOF)", bytes == 0);

    vfs_close(fd);
}

static void test_ramfs_get_file_data(void) {
    vfs_init();
    ramfs_init();

    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    ramfs_add_file("binary", data, 4);

    uint64_t size = 0;
    const uint8_t *ptr = ramfs_get_file_data("binary", &size);
    TEST("get_file_data returns non-NULL", ptr != NULL);
    TEST("get_file_data size is 4", size == 4);
    TEST("get_file_data pointer matches", ptr == data);
}

static void test_ramfs_get_file_data_missing(void) {
    vfs_init();
    ramfs_init();

    const uint8_t *ptr = ramfs_get_file_data("nonexistent", NULL);
    TEST("get_file_data for missing returns NULL", ptr == NULL);
}

static void test_vfs_readdir(void) {
    vfs_init();
    ramfs_init();

    ramfs_add_file("alpha", (const uint8_t *)"a", 1);
    ramfs_add_file("beta", (const uint8_t *)"b", 1);

    char name[32];
    int ret = vfs_readdir("/", 0, name, sizeof(name));
    TEST("readdir index 0 succeeds", ret == 0);
    TEST("readdir index 0 is 'alpha'", strcmp(name, "alpha") == 0);

    ret = vfs_readdir("/", 1, name, sizeof(name));
    TEST("readdir index 1 succeeds", ret == 0);
    TEST("readdir index 1 is 'beta'", strcmp(name, "beta") == 0);

    ret = vfs_readdir("/", 2, name, sizeof(name));
    TEST("readdir index 2 returns -1 (past end)", ret == -1);
}

void test_vfs_suite(void) {
    printf("=== VFS + RAMFS tests ===\n");
    test_vfs_init_ok();
    test_vfs_lookup_root();
    test_vfs_register_and_lookup();
    test_vfs_lookup_missing();
    test_vfs_open_close();
    test_vfs_open_missing();
    test_vfs_close_reserved();
    test_ramfs_add_and_read();
    test_ramfs_get_file_data();
    test_ramfs_get_file_data_missing();
    test_vfs_readdir();
}
