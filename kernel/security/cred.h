#ifndef CRED_H
#define CRED_H

#include "../common.h"

struct process; /* forward decl */

/* Standard Unix permission check: root bypasses all;
 * owner bits if euid matches i_uid; group bits if egid matches i_gid;
 * else other bits. */
bool cred_check_read(struct process *proc, uint16_t i_mode, uint16_t i_uid, uint16_t i_gid);
bool cred_check_write(struct process *proc, uint16_t i_mode, uint16_t i_uid, uint16_t i_gid);
bool cred_check_exec(struct process *proc, uint16_t i_mode, uint16_t i_uid, uint16_t i_gid);

#endif /* CRED_H */
