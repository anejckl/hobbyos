#include "ethernet.h"
#include "net.h"
#include "arp.h"
#include "ipv4.h"
#include "../drivers/e1000.h"
#include "../string.h"
#include "../debug/debug.h"

void ethernet_receive(struct netbuf *buf) {
    if (buf->len < ETH_HEADER_LEN) {
        netbuf_free(buf);
        return;
    }

    struct eth_header *hdr = (struct eth_header *)buf->data;
    uint16_t ethertype = ntohs(hdr->ethertype);

    /* Adjust offset past ethernet header */
    buf->offset = ETH_HEADER_LEN;

    switch (ethertype) {
    case ETHERTYPE_ARP:
        arp_receive(buf);
        break;
    case ETHERTYPE_IPV4:
        ipv4_receive(buf);
        break;
    default:
        netbuf_free(buf);
        break;
    }
}

int ethernet_send(const uint8_t *data, uint32_t len, const uint8_t *dst_mac, uint16_t ethertype) {
    uint8_t frame[1518];
    uint32_t total = ETH_HEADER_LEN + len;
    if (total > sizeof(frame))
        return -1;

    struct eth_header *hdr = (struct eth_header *)frame;
    memcpy(hdr->dst, dst_mac, 6);
    e1000_get_mac(hdr->src);
    hdr->ethertype = htons(ethertype);
    memcpy(frame + ETH_HEADER_LEN, data, len);

    return e1000_send_packet(frame, total);
}
