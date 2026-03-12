#include "common.h"
#include "string.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "drivers/fb.h"
#include "drivers/mouse.h"
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
#include "drivers/tty.h"
#include "drivers/device.h"
#include "fs/devfs.h"
#include "user_programs.h"
#include "autotest.h"
#include "drivers/pci.h"
#include "drivers/e1000.h"
#include "drivers/driver_model.h"
#include "drivers/blockcache.h"
#include "net/netbuf.h"
#include "net/net.h"
#include "net/tcp.h"
#include "memory/rmap.h"
#include "memory/swap.h"
#include "memory/pagecache.h"
#include "fs/journal.h"

/* Device init forward declarations */
extern void dev_null_init(void);
extern void dev_zero_init(void);
extern void dev_tty_init(void);
extern void dev_random_init(void);
extern void dev_input_init(void);

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

    /* Save MB2 info pointer for later FB parsing (after VMM/heap ready) */
    uint32_t mb2_info_phys = multiboot_info_phys;

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

    tty_init();
    vga_printf("[OK] TTY subsystem initialized\n");

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

    /* Reverse map + swap + page cache */
    rmap_init();
    swap_init();
    pagecache_init();
    vga_printf("[OK] VM subsystems (rmap, swap, pagecache) initialized\n");

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

    /* Device framework */
    device_init();
    driver_subsys_init();
    dev_null_init();
    dev_zero_init();
    dev_tty_init();
    dev_random_init();
    devfs_init();
    vga_printf("[OK] Device framework initialized (/dev)\n");

    /* Parse Multiboot2 tags for framebuffer */
    {
        uint8_t *mb2 = (uint8_t *)PHYS_TO_VIRT((uint64_t)mb2_info_phys);
        uint32_t total_size = *(uint32_t *)mb2;
        uint8_t *tag = mb2 + 8;
        uint8_t *end = mb2 + total_size;

        while (tag < end) {
            uint32_t tag_type = *(uint32_t *)tag;
            uint32_t tag_size = *(uint32_t *)(tag + 4);
            if (tag_type == 0) break; /* end tag */

            if (tag_type == 8) {
                /* Framebuffer tag */
                uint64_t fb_addr  = *(uint64_t *)(tag + 8);
                uint32_t fb_pitch = *(uint32_t *)(tag + 16);
                uint32_t fb_w     = *(uint32_t *)(tag + 20);
                uint32_t fb_h     = *(uint32_t *)(tag + 24);
                uint8_t  fb_bpp   = *(uint8_t  *)(tag + 28);
                uint8_t  fb_type  = *(uint8_t  *)(tag + 29);

                if (fb_type == 1 && fb_bpp == 32) {  /* RGB, 32bpp */
                    fb_init(fb_addr, fb_w, fb_h, fb_pitch, fb_bpp);
                    vga_printf("[OK] Framebuffer: %ux%u bpp=%u\n",
                               (uint64_t)fb_w, (uint64_t)fb_h, (uint64_t)fb_bpp);
                }
            }

            /* Tags are aligned to 8 bytes */
            tag += (tag_size + 7) & ~7U;
        }
    }

    /* PS/2 mouse */
    mouse_init();
    vga_printf("[OK] PS/2 mouse initialized\n");

    /* Input device nodes (/dev/input/keyboard, /dev/input/mouse0) */
    dev_input_init();
    vga_printf("[OK] Input device nodes registered\n");

    /* Block cache */
    bcache_init();
    vga_printf("[OK] Block cache initialized\n");

    /* ATA disk driver */
    ata_init();
    if (ata_disk_present()) {
        vga_printf("[OK] ATA disk detected\n");

        /* Journal recovery before ext2 init */
        journal_init();
        journal_recover();

        if (ext2_init() == 0)
            vga_printf("[OK] ext2 filesystem mounted\n");
        else
            vga_printf("[--] ext2: no valid filesystem found\n");
    } else {
        vga_printf("[--] No ATA disk detected\n");
    }

    /* PCI bus enumeration */
    pci_init();
    vga_printf("[OK] PCI bus enumerated\n");
    debug_printf("PCI bus enumerated\n");

    /* Network buffer pool */
    netbuf_init();

    /* E1000 NIC driver */
    e1000_init();
    if (e1000_is_initialized()) {
        uint8_t mac[6];
        e1000_get_mac(mac);
        vga_printf("[OK] e1000 NIC initialized (MAC %x:%x:%x:%x:%x:%x)\n",
                   (uint64_t)mac[0], (uint64_t)mac[1], (uint64_t)mac[2],
                   (uint64_t)mac[3], (uint64_t)mac[4], (uint64_t)mac[5]);
    } else {
        vga_printf("[--] e1000 NIC not found\n");
    }

    /* TCP state */
    tcp_init();

    /* Network stack */
    net_init();

    /* Network worker process (deferred packet processing from e1000 IRQ) */
    if (e1000_is_initialized()) {
        struct process *net = process_create("net_worker", net_worker_run);
        if (net)
            scheduler_add(net);
    }

    /* Mark kernel page boundary — pages below this are never freed by unref */
    pmm_set_kernel_end();

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
