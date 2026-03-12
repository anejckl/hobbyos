#include "test_main.h"
#include <string.h>
#include <stdbool.h>

/*
 * Minimal process stub for testing cred_check_* functions.
 * We replicate the credential fields and the cred logic here
 * so we can test on the host without pulling in the full kernel.
 */

struct test_process {
    uint16_t uid, gid;
    uint16_t euid, egid;
};

/* Replicate cred logic from kernel/security/cred.c for host testing */
static bool test_cred_check_read(struct test_process *proc, uint16_t i_mode,
                                  uint16_t i_uid, uint16_t i_gid) {
    if (proc->euid == 0) return true;
    if (proc->euid == i_uid) return (i_mode & 0400) != 0;
    if (proc->egid == i_gid) return (i_mode & 0040) != 0;
    return (i_mode & 0004) != 0;
}

static bool test_cred_check_write(struct test_process *proc, uint16_t i_mode,
                                   uint16_t i_uid, uint16_t i_gid) {
    if (proc->euid == 0) return true;
    if (proc->euid == i_uid) return (i_mode & 0200) != 0;
    if (proc->egid == i_gid) return (i_mode & 0020) != 0;
    return (i_mode & 0002) != 0;
}

static bool test_cred_check_exec(struct test_process *proc, uint16_t i_mode,
                                  uint16_t i_uid, uint16_t i_gid) {
    if (proc->euid == 0) return true;
    if (proc->euid == i_uid) return (i_mode & 0100) != 0;
    if (proc->egid == i_gid) return (i_mode & 0010) != 0;
    return (i_mode & 0001) != 0;
}

static void test_cred_root_bypass(void) {
    struct test_process root = { .uid = 0, .gid = 0, .euid = 0, .egid = 0 };

    /* Root bypasses all permission checks */
    TEST("root can read mode 0000", test_cred_check_read(&root, 0000, 100, 100));
    TEST("root can write mode 0000", test_cred_check_write(&root, 0000, 100, 100));
    TEST("root can exec mode 0000", test_cred_check_exec(&root, 0000, 100, 100));
    TEST("root can read mode 0644", test_cred_check_read(&root, 0644, 100, 100));
    TEST("root can write mode 0444", test_cred_check_write(&root, 0444, 100, 100));
}

static void test_cred_owner(void) {
    struct test_process user = { .uid = 1000, .gid = 1000, .euid = 1000, .egid = 1000 };

    /* Owner permissions (file owned by uid=1000, gid=2000) */
    TEST("owner can read 0644", test_cred_check_read(&user, 0644, 1000, 2000));
    TEST("owner can write 0644", test_cred_check_write(&user, 0644, 1000, 2000));
    TEST("owner cannot exec 0644", !test_cred_check_exec(&user, 0644, 1000, 2000));
    TEST("owner can exec 0755", test_cred_check_exec(&user, 0755, 1000, 2000));
    TEST("owner cannot read 0000", !test_cred_check_read(&user, 0000, 1000, 2000));
    TEST("owner cannot write 0444", !test_cred_check_write(&user, 0444, 1000, 2000));
    TEST("owner can read 0400", test_cred_check_read(&user, 0400, 1000, 2000));
    TEST("owner can write 0200", test_cred_check_write(&user, 0200, 1000, 2000));
    TEST("owner can exec 0100", test_cred_check_exec(&user, 0100, 1000, 2000));
}

static void test_cred_group(void) {
    struct test_process user = { .uid = 1000, .gid = 2000, .euid = 1000, .egid = 2000 };

    /* Group permissions (file owned by uid=500, gid=2000) */
    TEST("group can read 0040", test_cred_check_read(&user, 0040, 500, 2000));
    TEST("group can write 0020", test_cred_check_write(&user, 0020, 500, 2000));
    TEST("group can exec 0010", test_cred_check_exec(&user, 0010, 500, 2000));
    TEST("group cannot read 0004", !test_cred_check_read(&user, 0004, 500, 2000));
    TEST("group cannot write 0002", !test_cred_check_write(&user, 0002, 500, 2000));
    TEST("group cannot exec 0001", !test_cred_check_exec(&user, 0001, 500, 2000));
}

static void test_cred_other(void) {
    struct test_process user = { .uid = 1000, .gid = 1000, .euid = 1000, .egid = 1000 };

    /* Other permissions (file owned by uid=500, gid=500) */
    TEST("other can read 0004", test_cred_check_read(&user, 0004, 500, 500));
    TEST("other can write 0002", test_cred_check_write(&user, 0002, 500, 500));
    TEST("other can exec 0001", test_cred_check_exec(&user, 0001, 500, 500));
    TEST("other cannot read 0640", !test_cred_check_read(&user, 0640, 500, 500));
    TEST("other cannot write 0660", !test_cred_check_write(&user, 0660, 500, 500));
    TEST("other cannot exec 0770", !test_cred_check_exec(&user, 0770, 500, 500));
}

static void test_cred_euid(void) {
    /* Test that euid (not uid) is used for permission checks */
    struct test_process user = { .uid = 1000, .gid = 1000, .euid = 500, .egid = 1000 };

    /* File owned by euid=500 — should match as owner via euid */
    TEST("euid matches owner for read", test_cred_check_read(&user, 0400, 500, 2000));
    TEST("euid matches owner for write", test_cred_check_write(&user, 0200, 500, 2000));
    TEST("euid matches owner for exec", test_cred_check_exec(&user, 0100, 500, 2000));

    /* uid=1000 does NOT match file owner=500, but euid=500 does */
    TEST("uid does not match but euid does", test_cred_check_read(&user, 0400, 500, 2000));
}

static void test_cred_egid(void) {
    struct test_process user = { .uid = 1000, .gid = 1000, .euid = 1000, .egid = 500 };

    /* File owned by gid=500 — should match via egid */
    TEST("egid matches group for read", test_cred_check_read(&user, 0040, 2000, 500));
    TEST("egid matches group for write", test_cred_check_write(&user, 0020, 2000, 500));
    TEST("egid matches group for exec", test_cred_check_exec(&user, 0010, 2000, 500));
}

void test_cred_suite(void) {
    printf("=== Credential/permission tests ===\n");
    test_cred_root_bypass();
    test_cred_owner();
    test_cred_group();
    test_cred_other();
    test_cred_euid();
    test_cred_egid();
}
