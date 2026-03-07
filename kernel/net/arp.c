#include "arp.h"
#include "net.h"
#include "ethernet.h"
#include "../drivers/e1000.h"
#include "../string.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../debug/debug.h"

static struct arp_entry arp_table[ARP_TABLE_SIZE];
static struct process *arp_waiter = NULL;
static uint32_t arp_waiter_ip = 0;

void arp_init(void) {
    memset(arp_table, 0, sizeof(arp_table));
}

static struct arp_entry *arp_lookup(uint32_t ip) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip)
            return &arp_table[i];
    }
    return NULL;
}

static void arp_update(uint32_t ip, const uint8_t *mac) {
    /* Update existing entry */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip) {
            memcpy(arp_table[i].mac, mac, 6);
            return;
        }
    }
    /* Add new entry */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) {
            arp_table[i].ip = ip;
            memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].valid = true;
            return;
        }
    }
    /* Table full: overwrite first entry */
    arp_table[0].ip = ip;
    memcpy(arp_table[0].mac, mac, 6);
    arp_table[0].valid = true;
}

static void arp_send_request(uint32_t target_ip) {
    struct arp_header arp;
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    arp.hw_type = htons(1);      /* Ethernet */
    arp.proto_type = htons(0x0800); /* IPv4 */
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = htons(ARP_OP_REQUEST);
    e1000_get_mac(arp.sender_mac);
    arp.sender_ip = htonl(net_get_ip());
    memset(arp.target_mac, 0, 6);
    arp.target_ip = htonl(target_ip);

    ethernet_send((const uint8_t *)&arp, sizeof(arp), broadcast, ETHERTYPE_ARP);
}

static void arp_send_reply(const uint8_t *dst_mac, uint32_t dst_ip) {
    struct arp_header arp;

    arp.hw_type = htons(1);
    arp.proto_type = htons(0x0800);
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = htons(ARP_OP_REPLY);
    e1000_get_mac(arp.sender_mac);
    arp.sender_ip = htonl(net_get_ip());
    memcpy(arp.target_mac, dst_mac, 6);
    arp.target_ip = htonl(dst_ip);

    ethernet_send((const uint8_t *)&arp, sizeof(arp), dst_mac, ETHERTYPE_ARP);
}

void arp_receive(struct netbuf *buf) {
    if (buf->len - buf->offset < sizeof(struct arp_header)) {
        netbuf_free(buf);
        return;
    }

    struct arp_header *arp = (struct arp_header *)(buf->data + buf->offset);
    uint16_t opcode = ntohs(arp->opcode);
    uint32_t sender_ip = ntohl(arp->sender_ip);
    uint32_t target_ip = ntohl(arp->target_ip);

    /* Always learn from incoming ARP */
    arp_update(sender_ip, arp->sender_mac);

    if (opcode == ARP_OP_REQUEST && target_ip == net_get_ip()) {
        /* Someone is asking for our MAC */
        arp_send_reply(arp->sender_mac, sender_ip);
    }

    /* Wake ARP waiter if the reply we're looking for arrived */
    if (arp_waiter && arp_waiter_ip == sender_ip) {
        arp_waiter->state = PROCESS_READY;
        scheduler_add(arp_waiter);
        arp_waiter = NULL;
    }

    netbuf_free(buf);
}

int arp_resolve(uint32_t ip, uint8_t *mac_out) {
    /* If destination is not on our subnet, resolve gateway instead */
    uint32_t resolve_ip = ip;
    if ((ip & net_get_netmask()) != (net_get_ip() & net_get_netmask()))
        resolve_ip = net_get_gateway();

    /* Check cache first */
    struct arp_entry *entry = arp_lookup(resolve_ip);
    if (entry) {
        memcpy(mac_out, entry->mac, 6);
        return 0;
    }

    /* Send ARP request and block */
    struct process *cur = scheduler_get_current();
    arp_waiter = cur;
    arp_waiter_ip = resolve_ip;

    arp_send_request(resolve_ip);

    /* Block and wait for reply (retry up to 3 times) */
    for (int attempt = 0; attempt < 3; attempt++) {
        cur->state = PROCESS_BLOCKED;
        schedule();
        sti();

        entry = arp_lookup(resolve_ip);
        if (entry) {
            memcpy(mac_out, entry->mac, 6);
            arp_waiter = NULL;
            return 0;
        }

        /* Retry */
        arp_send_request(resolve_ip);
    }

    arp_waiter = NULL;
    return -1;
}

void arp_display_table(void) {
    /* Used by shell ifconfig/arp command - outputs via vga_printf */
    extern void vga_printf(const char *fmt, ...);
    vga_printf("ARP Table:\n");
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid) {
            uint32_t ip = arp_table[i].ip;
            vga_printf("  %u.%u.%u.%u -> %x:%x:%x:%x:%x:%x\n",
                       (uint64_t)((ip >> 24) & 0xFF),
                       (uint64_t)((ip >> 16) & 0xFF),
                       (uint64_t)((ip >> 8) & 0xFF),
                       (uint64_t)(ip & 0xFF),
                       (uint64_t)arp_table[i].mac[0],
                       (uint64_t)arp_table[i].mac[1],
                       (uint64_t)arp_table[i].mac[2],
                       (uint64_t)arp_table[i].mac[3],
                       (uint64_t)arp_table[i].mac[4],
                       (uint64_t)arp_table[i].mac[5]);
        }
    }
}
