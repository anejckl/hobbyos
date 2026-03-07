#include "e1000.h"
#include "pci.h"
#include "../common.h"
#include "../string.h"
#include "../memory/vmm.h"
#include "../memory/kheap.h"
#include "../arch/x86_64/isr.h"
#include "../arch/x86_64/pic.h"
#include "../net/netbuf.h"
#include "../net/net.h"
#include "../debug/debug.h"

/* MMIO virtual base for E1000 registers */
#define E1000_MMIO_BASE 0xFFFFFFFFA0000000ULL
#define E1000_MMIO_PAGES 32  /* 128KB */

/* Forward declaration for ethernet_receive */
extern void ethernet_receive(struct netbuf *buf);

static volatile uint8_t *mmio_base = NULL;
static uint8_t mac_addr[6];
static bool initialized = false;

/* Descriptor rings - statically allocated for reliable physical addresses */
static struct e1000_tx_desc tx_ring[E1000_NUM_TX_DESC] __attribute__((aligned(16)));
static struct e1000_rx_desc rx_ring[E1000_NUM_RX_DESC] __attribute__((aligned(16)));

/* TX packet buffers (static, in BSS) */
static uint8_t tx_buffers[E1000_NUM_TX_DESC][2048] __attribute__((aligned(16)));

/* RX packet buffers (static, in BSS) */
static uint8_t rx_buffers[E1000_NUM_RX_DESC][2048] __attribute__((aligned(16)));

static uint32_t tx_tail = 0;
static uint32_t rx_tail = 0;

static inline void e1000_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(mmio_base + reg) = val;
}

static inline uint32_t e1000_read(uint32_t reg) {
    return *(volatile uint32_t *)(mmio_base + reg);
}

static void e1000_irq_handler(struct interrupt_frame *frame) {
    (void)frame;

    uint32_t icr = e1000_read(E1000_ICR);

    if (icr & E1000_IMS_RXT0) {
        /* Enqueue received packets for deferred processing by net worker */
        while (true) {
            uint32_t cur = rx_tail;
            if (!(rx_ring[cur].status & E1000_RXD_STAT_DD))
                break;

            uint16_t len = rx_ring[cur].length;
            if (len > 0 && len <= NETBUF_SIZE) {
                struct netbuf *nb = netbuf_alloc();
                if (nb) {
                    memcpy(nb->data, rx_buffers[cur], len);
                    nb->len = len;
                    nb->offset = 0;
                    net_rx_enqueue(nb);
                }
            }

            /* Re-arm descriptor */
            rx_ring[cur].status = 0;
            rx_tail = (cur + 1) % E1000_NUM_RX_DESC;
            e1000_write(E1000_RDT, cur);
        }
    }

    /* EOI is already sent by isr_handler before dispatch */
}

void e1000_init(void) {
    struct pci_device *pci = pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (!pci) {
        debug_printf("e1000: device not found\n");
        return;
    }

    debug_printf("e1000: found at PCI %x:%x.%x, IRQ %u\n",
                 (uint64_t)pci->bus, (uint64_t)pci->device,
                 (uint64_t)pci->function, (uint64_t)pci->irq_line);

    /* Enable bus mastering for DMA */
    pci_enable_bus_mastering(pci);

    /* Map BAR0 (MMIO) into kernel virtual space */
    uint64_t bar0_phys = pci->bar[0] & 0xFFFFFFF0U;
    debug_printf("e1000: BAR0 physical = 0x%x\n", bar0_phys);

    for (int i = 0; i < E1000_MMIO_PAGES; i++) {
        vmm_map_page(
            E1000_MMIO_BASE + (uint64_t)i * PAGE_SIZE,
            bar0_phys + (uint64_t)i * PAGE_SIZE,
            PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE
        );
    }
    mmio_base = (volatile uint8_t *)E1000_MMIO_BASE;

    /* Reset device */
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | E1000_CTRL_RST);
    /* Wait for reset to complete */
    for (volatile int i = 0; i < 100000; i++);

    /* Set link up */
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | E1000_CTRL_SLU | E1000_CTRL_ASDE);

    /* Disable all interrupts first */
    e1000_write(E1000_IMC, 0xFFFFFFFF);

    /* Read MAC address from RAL/RAH */
    uint32_t ral = e1000_read(E1000_RAL);
    uint32_t rah = e1000_read(E1000_RAH);
    mac_addr[0] = (uint8_t)(ral);
    mac_addr[1] = (uint8_t)(ral >> 8);
    mac_addr[2] = (uint8_t)(ral >> 16);
    mac_addr[3] = (uint8_t)(ral >> 24);
    mac_addr[4] = (uint8_t)(rah);
    mac_addr[5] = (uint8_t)(rah >> 8);

    debug_printf("e1000: MAC address %x:%x:%x:%x:%x:%x\n",
                 (uint64_t)mac_addr[0], (uint64_t)mac_addr[1],
                 (uint64_t)mac_addr[2], (uint64_t)mac_addr[3],
                 (uint64_t)mac_addr[4], (uint64_t)mac_addr[5]);

    /* Clear multicast table */
    for (int i = 0; i < 128; i++)
        e1000_write(E1000_MTA + (uint32_t)i * 4, 0);

    /* Setup RX ring */
    memset(rx_ring, 0, sizeof(rx_ring));
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_ring[i].addr = KERNEL_VIRT_TO_PHYS((uint64_t)rx_buffers[i]);
        rx_ring[i].status = 0;
    }

    uint64_t rx_ring_phys = KERNEL_VIRT_TO_PHYS((uint64_t)rx_ring);
    e1000_write(E1000_RDBAL, (uint32_t)(rx_ring_phys & 0xFFFFFFFF));
    e1000_write(E1000_RDBAH, (uint32_t)(rx_ring_phys >> 32));
    e1000_write(E1000_RDLEN, (uint32_t)(E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc)));
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_NUM_RX_DESC - 1);
    rx_tail = 0;

    /* Enable receiver */
    e1000_write(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC | E1000_RCTL_BSIZE_2048);

    /* Setup TX ring */
    memset(tx_ring, 0, sizeof(tx_ring));
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_ring[i].addr = KERNEL_VIRT_TO_PHYS((uint64_t)tx_buffers[i]);
        tx_ring[i].status = E1000_TXD_STAT_DD; /* Mark as done */
    }

    uint64_t tx_ring_phys = KERNEL_VIRT_TO_PHYS((uint64_t)tx_ring);
    e1000_write(E1000_TDBAL, (uint32_t)(tx_ring_phys & 0xFFFFFFFF));
    e1000_write(E1000_TDBAH, (uint32_t)(tx_ring_phys >> 32));
    e1000_write(E1000_TDLEN, (uint32_t)(E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc)));
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    tx_tail = 0;

    /* Enable transmitter */
    e1000_write(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                (15 << 4) /* CT */ | (64 << 12) /* COLD */);

    /* Register IRQ handler and enable RX interrupt */
    uint8_t irq = pci->irq_line;
    isr_register_handler(32 + irq, e1000_irq_handler);
    pic_clear_mask(2);    /* Unmask cascade (IRQ2) for slave PIC delivery */
    pic_clear_mask(irq);
    e1000_write(E1000_IMS, E1000_IMS_RXT0);

    initialized = true;
    debug_printf("e1000: NIC initialized on IRQ %u\n", (uint64_t)irq);
}

int e1000_send_packet(const uint8_t *data, uint32_t len) {
    if (!initialized || !data || len == 0 || len > 1518)
        return -1;

    uint32_t cur = tx_tail;

    /* Wait for descriptor to be available */
    int timeout = 10000;
    while (!(tx_ring[cur].status & E1000_TXD_STAT_DD) && timeout > 0)
        timeout--;
    if (timeout == 0)
        return -1;

    /* Copy data to TX buffer */
    memcpy(tx_buffers[cur], data, len);

    tx_ring[cur].addr = KERNEL_VIRT_TO_PHYS((uint64_t)tx_buffers[cur]);
    tx_ring[cur].length = (uint16_t)len;
    tx_ring[cur].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    tx_ring[cur].status = 0;

    tx_tail = (cur + 1) % E1000_NUM_TX_DESC;
    e1000_write(E1000_TDT, tx_tail);

    return 0;
}

void e1000_get_mac(uint8_t *mac_out) {
    memcpy(mac_out, mac_addr, 6);
}

bool e1000_is_initialized(void) {
    return initialized;
}
