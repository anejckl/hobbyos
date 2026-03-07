#ifndef E1000_H
#define E1000_H

#include "../common.h"

#define E1000_VENDOR_ID  0x8086
#define E1000_DEVICE_ID  0x100E

/* E1000 register offsets */
#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_ICR       0x00C0
#define E1000_IMS       0x00D0
#define E1000_IMC       0x00D8
#define E1000_RCTL      0x0100
#define E1000_TCTL      0x0400
#define E1000_RDBAL     0x2800
#define E1000_RDBAH     0x2804
#define E1000_RDLEN     0x2808
#define E1000_RDH       0x2810
#define E1000_RDT       0x2818
#define E1000_TDBAL     0x3800
#define E1000_TDBAH     0x3804
#define E1000_TDLEN     0x3808
#define E1000_TDH       0x3810
#define E1000_TDT       0x3818
#define E1000_RAL       0x5400
#define E1000_RAH       0x5404
#define E1000_MTA       0x5200

/* CTRL bits */
#define E1000_CTRL_RST   (1 << 26)
#define E1000_CTRL_SLU   (1 << 6)
#define E1000_CTRL_ASDE  (1 << 5)

/* RCTL bits */
#define E1000_RCTL_EN    (1 << 1)
#define E1000_RCTL_BAM   (1 << 15)
#define E1000_RCTL_SECRC (1 << 26)
#define E1000_RCTL_BSIZE_2048 0  /* default */

/* TCTL bits */
#define E1000_TCTL_EN    (1 << 1)
#define E1000_TCTL_PSP   (1 << 3)

/* IMS bits */
#define E1000_IMS_RXT0   (1 << 7)

/* TX command bits */
#define E1000_TXD_CMD_EOP  (1 << 0)
#define E1000_TXD_CMD_IFCS (1 << 1)
#define E1000_TXD_CMD_RS   (1 << 3)

/* TX/RX status bits */
#define E1000_TXD_STAT_DD  (1 << 0)
#define E1000_RXD_STAT_DD  (1 << 0)
#define E1000_RXD_STAT_EOP (1 << 1)

#define E1000_NUM_TX_DESC 32
#define E1000_NUM_RX_DESC 32

/* TX descriptor (legacy) */
struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

/* RX descriptor (legacy) */
struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

void e1000_init(void);
int e1000_send_packet(const uint8_t *data, uint32_t len);
void e1000_get_mac(uint8_t *mac_out);
bool e1000_is_initialized(void);

#endif /* E1000_H */
