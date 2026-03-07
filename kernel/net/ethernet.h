#ifndef ETHERNET_H
#define ETHERNET_H

#include "../common.h"
#include "netbuf.h"

#define ETH_HEADER_LEN 14
#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPV4 0x0800

struct eth_header {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
} __attribute__((packed));

void ethernet_receive(struct netbuf *buf);
int ethernet_send(const uint8_t *data, uint32_t len, const uint8_t *dst_mac, uint16_t ethertype);

#endif /* ETHERNET_H */
