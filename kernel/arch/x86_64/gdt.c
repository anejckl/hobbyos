#include "gdt.h"
#include "tss.h"
#include "../../string.h"

/* GDT: null + code64 + data64 + TSS(16 bytes) = 4 regular entries + 1 TSS (takes 2 slots) */
/* Total: 5 GDT entries worth of space */
static struct gdt_entry gdt_entries[5];
static struct gdt_pointer gdt_ptr;

static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t granularity) {
    gdt_entries[index].base_low    = base & 0xFFFF;
    gdt_entries[index].base_mid    = (base >> 16) & 0xFF;
    gdt_entries[index].base_high   = (base >> 24) & 0xFF;
    gdt_entries[index].limit_low   = limit & 0xFFFF;
    gdt_entries[index].access      = access;
    gdt_entries[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
}

static void gdt_set_tss(int index, struct tss *tss_ptr) {
    uint64_t base = (uint64_t)tss_ptr;
    uint32_t limit = sizeof(struct tss) - 1;

    struct tss_descriptor *tss_desc = (struct tss_descriptor *)&gdt_entries[index];

    tss_desc->limit_low   = limit & 0xFFFF;
    tss_desc->base_low    = base & 0xFFFF;
    tss_desc->base_mid    = (base >> 16) & 0xFF;
    tss_desc->access      = 0x89;   /* Present, 64-bit TSS Available */
    tss_desc->granularity = ((limit >> 16) & 0x0F);
    tss_desc->base_high   = (base >> 24) & 0xFF;
    tss_desc->base_upper  = (uint32_t)(base >> 32);
    tss_desc->reserved    = 0;
}

void gdt_init(void) {
    /* Initialize TSS first */
    tss_init();

    /* Null descriptor */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Kernel code segment: Execute/Read, Ring 0, 64-bit
     * Access: Present(7) + DPL0(5-6) + S(4) + Code(3) + Exec(1) + Read(0)
     * = 0x9A
     * Granularity: L bit(5) for 64-bit = 0x20 */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0x20);

    /* Kernel data segment: Read/Write, Ring 0
     * Access: Present(7) + DPL0(5-6) + S(4) + Data(0) + Write(1)
     * = 0x92 */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0x00);

    /* TSS (takes entries 3 and 4, as it's 16 bytes in 64-bit mode) */
    gdt_set_tss(3, tss_get());

    /* Set up GDT pointer */
    gdt_ptr.limit = sizeof(gdt_entries) - 1;
    gdt_ptr.base  = (uint64_t)&gdt_entries;

    /* Load GDT and reload segments */
    gdt_flush((uint64_t)&gdt_ptr);

    /* Load TSS */
    tss_flush(GDT_TSS);
}
