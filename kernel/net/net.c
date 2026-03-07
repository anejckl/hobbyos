#include "net.h"
#include "netbuf.h"
#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../debug/debug.h"

static uint32_t our_ip = NET_IP;
static uint32_t our_netmask = NET_NETMASK;
static uint32_t our_gateway = NET_GATEWAY;

/* Received packet queue — filled by e1000 IRQ, drained by net worker process */
#define NET_RX_QUEUE_SIZE 64
static struct netbuf *rx_queue[NET_RX_QUEUE_SIZE];
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;
static struct process *net_worker_proc = NULL;

uint16_t ip_checksum(const void *data, uint32_t len) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1)
        sum += *(const uint8_t *)ptr;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum);
}

void net_init(void) {
    arp_init();
    debug_printf("network: stack ready, IP 10.0.2.15\n");
}

void net_tick(void) {
    tcp_tick();
}

uint32_t net_get_ip(void) {
    return our_ip;
}

uint32_t net_get_gateway(void) {
    return our_gateway;
}

uint32_t net_get_netmask(void) {
    return our_netmask;
}

void net_rx_enqueue(struct netbuf *nb) {
    uint32_t next = (rx_tail + 1) % NET_RX_QUEUE_SIZE;
    if (next == rx_head) {
        /* Queue full, drop packet */
        netbuf_free(nb);
        return;
    }
    rx_queue[rx_tail] = nb;
    rx_tail = next;

    /* Wake the net worker if it's blocked */
    if (net_worker_proc && net_worker_proc->state == PROCESS_BLOCKED) {
        net_worker_proc->state = PROCESS_READY;
        scheduler_add(net_worker_proc);
    }
}

void net_worker_run(void) {
    /* Re-enable interrupts — first scheduled via context_switch from
     * PIT ISR with IF=0 (same pattern as shell_run/autotest_run). */
    sti();

    for (;;) {
        /* Process all queued packets */
        while (rx_head != rx_tail) {
            struct netbuf *nb = rx_queue[rx_head];
            rx_head = (rx_head + 1) % NET_RX_QUEUE_SIZE;
            ethernet_receive(nb);
        }

        /* Block until woken by IRQ handler */
        cli();
        if (rx_head == rx_tail) {
            struct process *cur = scheduler_get_current();
            net_worker_proc = cur;
            cur->state = PROCESS_BLOCKED;
            schedule();
            sti();
        } else {
            sti();
        }
    }
}
