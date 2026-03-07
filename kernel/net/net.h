#ifndef NET_H
#define NET_H

#include "../common.h"

/* QEMU SLIRP defaults */
#define NET_IP          0x0A00020FU  /* 10.0.2.15 */
#define NET_NETMASK     0xFFFFFF00U  /* 255.255.255.0 */
#define NET_GATEWAY     0x0A000202U  /* 10.0.2.2 */

/* Byte order helpers (x86 is little-endian) */
static inline uint16_t htons(uint16_t val) {
    return (uint16_t)((val >> 8) | (val << 8));
}

static inline uint16_t ntohs(uint16_t val) {
    return htons(val);
}

static inline uint32_t htonl(uint32_t val) {
    return ((val >> 24) & 0xFF) |
           ((val >> 8) & 0xFF00) |
           ((val << 8) & 0xFF0000) |
           ((val << 24) & 0xFF000000);
}

static inline uint32_t ntohl(uint32_t val) {
    return htonl(val);
}

/* Ones-complement checksum (used by IPv4, ICMP, TCP, UDP) */
uint16_t ip_checksum(const void *data, uint32_t len);

void net_init(void);
void net_tick(void);

/* Get our IP/MAC */
uint32_t net_get_ip(void);
uint32_t net_get_gateway(void);
uint32_t net_get_netmask(void);

/* Deferred packet processing: IRQ handler enqueues, worker process dequeues */
struct netbuf;
void net_rx_enqueue(struct netbuf *nb);
void net_worker_run(void);

#endif /* NET_H */
