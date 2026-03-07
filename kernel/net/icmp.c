#include "icmp.h"
#include "net.h"
#include "ipv4.h"
#include "../string.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../drivers/pit.h"
#include "../debug/debug.h"

struct ping_state ping_state;

void icmp_receive(struct netbuf *buf, uint32_t src_ip) {
    uint8_t *payload = buf->data + buf->offset;
    uint32_t remaining = buf->len - buf->offset;

    if (remaining < sizeof(struct icmp_header)) {
        netbuf_free(buf);
        return;
    }

    struct icmp_header *hdr = (struct icmp_header *)payload;

    if (hdr->type == ICMP_ECHO_REQUEST) {
        /* Reply to ping */
        hdr->type = ICMP_ECHO_REPLY;
        hdr->checksum = 0;
        hdr->checksum = ip_checksum(hdr, remaining);
        ipv4_send(payload, remaining, src_ip, IP_PROTO_ICMP);
    } else if (hdr->type == ICMP_ECHO_REPLY) {
        /* Wake ping waiter */
        if (ping_state.waiter &&
            ntohs(hdr->id) == ping_state.id &&
            ntohs(hdr->sequence) == ping_state.seq) {
            ping_state.received = true;
            ping_state.recv_tick = pit_get_ticks();
            ping_state.waiter->state = PROCESS_READY;
            scheduler_add(ping_state.waiter);
            ping_state.waiter = NULL;
        }
    }

    netbuf_free(buf);
}

int icmp_send_echo(uint32_t dst_ip, uint16_t id, uint16_t seq) {
    uint8_t packet[64];
    struct icmp_header *hdr = (struct icmp_header *)packet;

    memset(packet, 0, sizeof(packet));
    hdr->type = ICMP_ECHO_REQUEST;
    hdr->code = 0;
    hdr->id = htons(id);
    hdr->sequence = htons(seq);
    hdr->checksum = 0;
    hdr->checksum = ip_checksum(hdr, sizeof(packet));

    return ipv4_send(packet, sizeof(packet), dst_ip, IP_PROTO_ICMP);
}
