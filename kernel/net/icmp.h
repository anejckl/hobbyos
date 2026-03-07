#ifndef ICMP_H
#define ICMP_H

#include "../common.h"
#include "netbuf.h"

struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed));

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

void icmp_receive(struct netbuf *buf, uint32_t src_ip);
int icmp_send_echo(uint32_t dst_ip, uint16_t id, uint16_t seq);

/* Ping state for shell command */
struct ping_state {
    struct process *waiter;
    uint16_t id;
    uint16_t seq;
    bool received;
    uint64_t send_tick;
    uint64_t recv_tick;
};

extern struct ping_state ping_state;

#endif /* ICMP_H */
