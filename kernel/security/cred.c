#include "cred.h"
#include "../process/process.h"

/* Unix permission bits within i_mode (lower 12 bits):
 * Bits 8-6: owner rwx
 * Bits 5-3: group rwx
 * Bits 2-0: other rwx
 */

bool cred_check_read(struct process *proc, uint16_t i_mode, uint16_t i_uid, uint16_t i_gid) {
    /* Root bypasses all */
    if (proc->euid == 0)
        return true;

    /* Owner */
    if (proc->euid == i_uid)
        return (i_mode & 0400) != 0;  /* S_IRUSR */

    /* Group */
    if (proc->egid == i_gid)
        return (i_mode & 0040) != 0;  /* S_IRGRP */

    /* Other */
    return (i_mode & 0004) != 0;      /* S_IROTH */
}

bool cred_check_write(struct process *proc, uint16_t i_mode, uint16_t i_uid, uint16_t i_gid) {
    if (proc->euid == 0)
        return true;

    if (proc->euid == i_uid)
        return (i_mode & 0200) != 0;  /* S_IWUSR */

    if (proc->egid == i_gid)
        return (i_mode & 0020) != 0;  /* S_IWGRP */

    return (i_mode & 0002) != 0;      /* S_IWOTH */
}

bool cred_check_exec(struct process *proc, uint16_t i_mode, uint16_t i_uid, uint16_t i_gid) {
    if (proc->euid == 0)
        return true;

    if (proc->euid == i_uid)
        return (i_mode & 0100) != 0;  /* S_IXUSR */

    if (proc->egid == i_gid)
        return (i_mode & 0010) != 0;  /* S_IXGRP */

    return (i_mode & 0001) != 0;      /* S_IXOTH */
}
