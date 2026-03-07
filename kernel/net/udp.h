#ifndef UDP_H
#define UDP_H

#include "../common.h"
#include "netbuf.h"

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

void udp_receive(struct netbuf *buf, uint32_t src_ip);
int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
             const uint8_t *data, uint32_t len);

#endif /* UDP_H */
