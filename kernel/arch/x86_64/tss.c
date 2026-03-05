#include "tss.h"
#include "../../string.h"

extern char kernel_stack_top[];

static struct tss tss_entry;

void tss_init(void) {
    memset(&tss_entry, 0, sizeof(struct tss));
    tss_entry.rsp0 = (uint64_t)kernel_stack_top;
    tss_entry.iopb_offset = sizeof(struct tss);
}

void tss_set_rsp0(uint64_t rsp0) {
    tss_entry.rsp0 = rsp0;
}

struct tss *tss_get(void) {
    return &tss_entry;
}
