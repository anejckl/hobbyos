#ifndef USERMODE_H
#define USERMODE_H

#include "../../common.h"

/* Enter ring 3 user mode via IRETQ.
 * entry      - user code entry point (RIP)
 * user_stack - user stack pointer (RSP)
 * cs         - user code segment selector (e.g. GDT_USER_CODE_RPL3)
 * ss         - user data segment selector (e.g. GDT_USER_DATA_RPL3)
 *
 * This function never returns. */
extern void enter_usermode(uint64_t entry, uint64_t user_stack,
                           uint64_t cs, uint64_t ss);

#endif /* USERMODE_H */
