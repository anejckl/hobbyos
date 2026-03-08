#include "ipv4.h"
#include "net.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "../string.h"
#include "../debug/debug.h"

static uint16_t ip_id_counter = 1;

void ipv4_receive(struct netbuf *buf) {
    uint8_t *payload = buf->data + buf->offset;
    uint32_t remaining = buf->len - buf->offset;

    if (remaining < sizeof(struct ipv4_header)) {
        netbuf_free(buf);
        return;
    }

    struct ipv4_header *hdr = (struct ipv4_header *)payload;
    uint8_t ihl = (hdr->version_ihl & 0x0F) * 4;
    uint16_t total = ntohs(hdr->total_length);

    /* Verify checksum */
    if (ip_checksum(hdr, ihl) != 0) {
        netbuf_free(buf);
        return;
    }

    /* Check destination is us or broadcast */
    uint32_t dst = ntohl(hdr->dst_ip);
    if (dst != net_get_ip() && dst != 0xFFFFFFFF) {
        netbuf_free(buf);
        return;
    }

    uint32_t src_ip = ntohl(hdr->src_ip);
    buf->offset += ihl;

    switch (hdr->protocol) {
    case IP_PROTO_ICMP:
        icmp_receive(buf, src_ip);
        break;
    case IP_PROTO_UDP:
        udp_receive(buf, src_ip);
        break;
    case IP_PROTO_TCP:
        tcp_receive(buf, src_ip, total - ihl);
        break;
    default:
        netbuf_free(buf);
        break;
    }
}

int ipv4_send(const uint8_t *data, uint32_t len, uint32_t dst_ip, uint8_t protocol) {
    static uint8_t packet[1500];
    uint32_t total = sizeof(struct ipv4_header) + len;
    if (total > sizeof(packet))
        return -1;

    struct ipv4_header *hdr = (struct ipv4_header *)packet;
    hdr->version_ihl = 0x45; /* IPv4, IHL=5 (20 bytes) */
    hdr->tos = 0;
    hdr->total_length = htons((uint16_t)total);
    hdr->identification = htons(ip_id_counter++);
    hdr->flags_fragment = htons(0x4000); /* Don't Fragment */
    hdr->ttl = 64;
    hdr->protocol = protocol;
    hdr->checksum = 0;
    hdr->src_ip = htonl(net_get_ip());
    hdr->dst_ip = htonl(dst_ip);
    hdr->checksum = ip_checksum(hdr, sizeof(struct ipv4_header));

    memcpy(packet + sizeof(struct ipv4_header), data, len);

    /* Resolve destination MAC via ARP */
    uint8_t dst_mac[6];
    if (arp_resolve(dst_ip, dst_mac) < 0)
        return -1;

    return ethernet_send(packet, total, dst_mac, ETHERTYPE_IPV4);
}
