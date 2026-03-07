#include "udp.h"
#include "net.h"
#include "ipv4.h"
#include "socket.h"
#include "../string.h"
#include "../debug/debug.h"

void udp_receive(struct netbuf *buf, uint32_t src_ip) {
    uint8_t *payload = buf->data + buf->offset;
    uint32_t remaining = buf->len - buf->offset;

    if (remaining < sizeof(struct udp_header)) {
        netbuf_free(buf);
        return;
    }

    struct udp_header *hdr = (struct udp_header *)payload;
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t udp_len = ntohs(hdr->length);
    uint32_t data_len = udp_len - sizeof(struct udp_header);

    uint8_t *data = payload + sizeof(struct udp_header);

    /* Deliver to socket layer */
    socket_udp_deliver(dst_port, src_ip, src_port, data, data_len);

    netbuf_free(buf);
}

int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
             const uint8_t *data, uint32_t len) {
    uint8_t packet[1472 + sizeof(struct udp_header)];
    uint32_t total = sizeof(struct udp_header) + len;
    if (total > sizeof(packet))
        return -1;

    struct udp_header *hdr = (struct udp_header *)packet;
    hdr->src_port = htons(src_port);
    hdr->dst_port = htons(dst_port);
    hdr->length = htons((uint16_t)total);
    hdr->checksum = 0; /* Skip UDP checksum (valid for IPv4) */

    memcpy(packet + sizeof(struct udp_header), data, len);

    return ipv4_send(packet, total, dst_ip, IP_PROTO_UDP);
}
