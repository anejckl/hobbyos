#ifndef ARP_H
#define ARP_H

#include "../common.h"
#include "netbuf.h"

#define ARP_TABLE_SIZE 32

struct arp_entry {
    uint32_t ip;
    uint8_t mac[6];
    bool valid;
    uint32_t timestamp;
};

struct arp_header {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t hw_len;
    uint8_t proto_len;
    uint16_t opcode;
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} __attribute__((packed));

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

void arp_init(void);
void arp_receive(struct netbuf *buf);
int arp_resolve(uint32_t ip, uint8_t *mac_out);
void arp_display_table(void);

#endif /* ARP_H */
