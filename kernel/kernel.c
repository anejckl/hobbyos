#include "common.h"
#include "string.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/pic.h"
#include "interrupts/interrupts.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/kheap.h"
#include "process/process.h"
#include "scheduler/scheduler.h"
#include "syscall/syscall.h"
#include "shell/shell.h"
#include "debug/debug.h"
#include "fs/vfs.h"
#include "fs/ramfs.h"
#include "fs/procfs.h"
#include "fs/ext2.h"
#include "drivers/ata.h"
#include "user_programs.h"
#include "autotest.h"

/* Multiboot2 constants */
#define MULTIBOOT2_MAGIC 0x36D76289

void kpanic(const char *msg) {
    cli();
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_printf("\n*** KERNEL PANIC: %s ***\n", msg);
    debug_printf("KERNEL PANIC: %s\n", msg);
    for (;;) hlt();
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_phys) {
    /* Phase 1: VGA output */
    vga_init();

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_printf("HobbyOS booting...\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* Phase 8 (early): Debug serial output */
    debug_init();
    debug_printf("HobbyOS: Serial debug output initialized\n");

    /* Verify multiboot2 */
    if (multiboot_magic != MULTIBOOT2_MAGIC) {
        kpanic("Invalid Multiboot2 magic number!");
    }
    vga_printf("[OK] Multiboot2 verified (info at 0x%x)\n", (uint64_t)multiboot_info_phys);
    debug_printf("Multiboot2 info at physical 0x%x\n", (uint64_t)multiboot_info_phys);

    /* Phase 2: GDT + TSS */
    gdt_init();
    vga_printf("[OK] GDT initialized\n");
    debug_printf("GDT initialized\n");

    /* Phase 3: IDT + PIC + Interrupts */
    pic_init();
    vga_printf("[OK] PIC remapped\n");
    debug_printf("PIC remapped\n");

    idt_init();
    vga_printf("[OK] IDT initialized\n");
    debug_printf("IDT initialized\n");

    interrupts_init();
    vga_printf("[OK] Interrupt handlers registered\n");
    debug_printf("Interrupt handlers registered\n");

    /* Phase 4: Timer + Keyboard */
    pit_init(100);  /* 100 Hz */
    vga_printf("[OK] PIT timer at 100Hz\n");
    debug_printf("PIT initialized at 100Hz\n");

    keyboard_init();
    vga_printf("[OK] Keyboard driver initialized\n");
    debug_printf("Keyboard initialized\n");

    /* Phase 5: Physical Memory Manager */
    pmm_init(multiboot_info_phys);
    vga_printf("[OK] PMM: %u free pages (%u MB free)\n",
               (uint64_t)pmm_get_free_pages(),
               (uint64_t)(pmm_get_free_pages() * 4 / 1024));
    debug_printf("PMM: %u free pages\n", (uint64_t)pmm_get_free_pages());

    /* Phase 6: Virtual Memory Manager + Kernel Heap */
    vmm_init();
    vga_printf("[OK] VMM initialized\n");
    debug_printf("VMM initialized\n");

    kheap_init();
    vga_printf("[OK] Kernel heap initialized\n");
    debug_printf("Kernel heap initialized\n");

    /* Phase 7: Process + Scheduler */
    process_init();
    vga_printf("[OK] Process subsystem initialized\n");
    debug_printf("Process subsystem initialized\n");

    scheduler_init();
    vga_printf("[OK] Scheduler initialized\n");
    debug_printf("Scheduler initialized\n");

    /* Syscall interface */
    syscall_init();
    vga_printf("[OK] Syscall handler registered\n");
    debug_printf("Syscall handler registered\n");

    /* Filesystem */
    vfs_init();
    vga_printf("[OK] VFS initialized\n");

    ramfs_init();
    vga_printf("[OK] RAMFS mounted\n");

    user_programs_populate_ramfs();
    vga_printf("[OK] User programs loaded into RAMFS\n");

    procfs_init();
    vga_printf("[OK] Procfs mounted at /proc\n");

    /* ATA disk driver */
    ata_init();
    if (ata_disk_present()) {
        vga_printf("[OK] ATA disk detected\n");
        if (ext2_init() == 0)
            vga_printf("[OK] ext2 filesystem mounted\n");
        else
            vga_printf("[--] ext2: no valid filesystem found\n");
    } else {
        vga_printf("[--] No ATA disk detected\n");
    }

    /* Enable interrupts */
    sti();
    vga_printf("[OK] Interrupts enabled\n\n");
    debug_printf("Interrupts enabled\n");

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_printf("Welcome to HobbyOS!\n");
    vga_printf("Type 'help' for a list of commands.\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* Auto-run integration tests */
    {
        struct process *autotest = process_create("autotest", autotest_run);
        if (autotest)
            scheduler_add(autotest);
    }

    /* Phase 8: Start shell as a scheduled kernel process */
    {
        struct process *shell = process_create("shell", shell_run);
        if (!shell)
            kpanic("Failed to create shell process");
        scheduler_add(shell);
    }

    /* kernel_main becomes idle loop */
    for (;;) hlt();
}
