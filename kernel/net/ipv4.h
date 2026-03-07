#ifndef IPV4_H
#define IPV4_H

#include "../common.h"
#include "netbuf.h"

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

struct ipv4_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

void ipv4_receive(struct netbuf *buf);
int ipv4_send(const uint8_t *data, uint32_t len, uint32_t dst_ip, uint8_t protocol);

#endif /* IPV4_H */
