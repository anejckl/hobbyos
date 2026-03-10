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

/* Like enter_usermode, but also sets RDI and RSI in the user-mode context.
 * user_rdi - value for RDI (e.g. argc)
 * user_rsi - value for RSI (e.g. user-space argv pointer)
 *
 * This function never returns. */
extern void enter_usermode_args(uint64_t entry, uint64_t user_stack,
                                uint64_t cs, uint64_t ss,
                                uint64_t user_rdi, uint64_t user_rsi);

#endif /* USERMODE_H */
