#ifndef TSS_H
#define TSS_H

#include "../../common.h"

/* 64-bit Task State Segment (104 bytes) */
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;          /* Stack pointer for ring 0 */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;          /* Interrupt Stack Table entries */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

void tss_init(void);
void tss_set_rsp0(uint64_t rsp0);
struct tss *tss_get(void);

#endif /* TSS_H */
