#include "tcp.h"
#include "net.h"
#include "ipv4.h"
#include "../string.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../drivers/pit.h"
#include "../debug/debug.h"
#include "../signal/signal.h"

static struct tcp_connection conns[TCP_MAX_CONNS];

/* TCP pseudo-header for checksum */
struct tcp_pseudo {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_length;
} __attribute__((packed));

void tcp_init(void) {
    memset(conns, 0, sizeof(conns));
}

int tcp_alloc_conn(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!conns[i].in_use) {
            memset(&conns[i], 0, sizeof(conns[i]));
            conns[i].in_use = true;
            conns[i].state = TCP_CLOSED;
            conns[i].parent_idx = -1;
            return i;
        }
    }
    return -1;
}

struct tcp_connection *tcp_get_conn(int idx) {
    if (idx < 0 || idx >= TCP_MAX_CONNS || !conns[idx].in_use)
        return NULL;
    return &conns[idx];
}

static uint16_t tcp_checksum(const void *tcp_data, uint32_t tcp_len,
                             uint32_t src_ip, uint32_t dst_ip) {
    struct tcp_pseudo pseudo;
    pseudo.src_ip = htonl(src_ip);
    pseudo.dst_ip = htonl(dst_ip);
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_TCP;
    pseudo.tcp_length = htons((uint16_t)tcp_len);

    uint32_t sum = 0;
    const uint8_t *pb = (const uint8_t *)&pseudo;
    for (uint32_t i = 0; i + 1 < sizeof(pseudo); i += 2)
        sum += (uint16_t)((uint16_t)pb[i] | ((uint16_t)pb[i + 1] << 8));
    if (sizeof(pseudo) & 1)
        sum += pb[sizeof(pseudo) - 1];

    const uint16_t *tp = (const uint16_t *)tcp_data;
    uint32_t remaining = tcp_len;
    while (remaining > 1) {
        sum += *tp++;
        remaining -= 2;
    }
    if (remaining == 1)
        sum += *(const uint8_t *)tp;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum);
}

static int tcp_send_segment(int idx, uint8_t flags, const uint8_t *data, uint32_t data_len) {
    struct tcp_connection *c = &conns[idx];
    uint8_t packet[1500];
    uint32_t hdr_len = sizeof(struct tcp_header);
    uint32_t total = hdr_len + data_len;
    if (total > sizeof(packet))
        return -1;

    struct tcp_header *hdr = (struct tcp_header *)packet;
    memset(hdr, 0, sizeof(*hdr));
    hdr->src_port = htons(c->local_port);
    hdr->dst_port = htons(c->remote_port);
    hdr->seq_num = htonl(c->snd_nxt);
    hdr->ack_num = htonl(c->rcv_nxt);
    hdr->data_offset = (uint8_t)((hdr_len / 4) << 4);
    hdr->flags = flags;
    hdr->window = htons(TCP_WINDOW_SIZE);
    hdr->checksum = 0;
    hdr->urgent = 0;

    if (data && data_len > 0)
        memcpy(packet + hdr_len, data, data_len);

    hdr->checksum = tcp_checksum(packet, total, c->local_ip, c->remote_ip);

    /* Advance sequence number */
    if (flags & TCP_SYN) c->snd_nxt++;
    if (flags & TCP_FIN) c->snd_nxt++;
    c->snd_nxt += data_len;

    return ipv4_send(packet, total, c->remote_ip, IP_PROTO_TCP);
}

static int find_connection(uint32_t src_ip, uint16_t src_port,
                           uint32_t dst_ip __attribute__((unused)),
                           uint16_t dst_port) {
    /* Find exact match first */
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (conns[i].in_use &&
            conns[i].remote_ip == src_ip &&
            conns[i].remote_port == src_port &&
            conns[i].local_port == dst_port)
            return i;
    }
    /* Find listening socket */
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (conns[i].in_use &&
            conns[i].state == TCP_LISTEN &&
            conns[i].local_port == dst_port)
            return i;
    }
    return -1;
}

static void wake_process(struct process **pp) {
    if (*pp) {
        (*pp)->state = PROCESS_READY;
        scheduler_add(*pp);
        *pp = NULL;
    }
}

void tcp_receive(struct netbuf *buf, uint32_t src_ip, uint32_t tcp_len) {
    uint8_t *payload = buf->data + buf->offset;
    uint32_t remaining = buf->len - buf->offset;
    (void)tcp_len;

    if (remaining < sizeof(struct tcp_header)) {
        netbuf_free(buf);
        return;
    }

    struct tcp_header *hdr = (struct tcp_header *)payload;
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint32_t seq = ntohl(hdr->seq_num);
    uint32_t ack = ntohl(hdr->ack_num);
    uint8_t flags = hdr->flags;
    uint8_t data_off = (hdr->data_offset >> 4) * 4;
    uint32_t data_len = remaining - data_off;
    uint8_t *data = payload + data_off;

    int idx = find_connection(src_ip, src_port, net_get_ip(), dst_port);
    if (idx < 0) {
        /* Send RST for unknown connections */
        netbuf_free(buf);
        return;
    }

    struct tcp_connection *c = &conns[idx];

    switch (c->state) {
    case TCP_LISTEN:
        if (flags & TCP_SYN) {
            /* Allocate new connection for incoming SYN */
            int new_idx = tcp_alloc_conn();
            if (new_idx < 0) {
                netbuf_free(buf);
                return;
            }
            struct tcp_connection *nc = &conns[new_idx];
            nc->local_ip = net_get_ip();
            nc->local_port = c->local_port;
            nc->remote_ip = src_ip;
            nc->remote_port = src_port;
            nc->rcv_nxt = seq + 1;
            nc->snd_nxt = pit_get_ticks() * 12345 + 1; /* Simple ISN */
            nc->snd_una = nc->snd_nxt;
            nc->state = TCP_SYN_RECEIVED;
            nc->parent_idx = idx;

            /* Send SYN-ACK */
            tcp_send_segment(new_idx, TCP_SYN | TCP_ACK, NULL, 0);
        }
        break;

    case TCP_SYN_RECEIVED:
        if (flags & TCP_ACK) {
            c->snd_una = ack;
            c->state = TCP_ESTABLISHED;

            /* Add to parent accept queue */
            if (c->parent_idx >= 0) {
                struct tcp_connection *parent = &conns[c->parent_idx];
                if (parent->accept_count < TCP_ACCEPT_QUEUE) {
                    parent->accept_queue[parent->accept_tail] = idx;
                    parent->accept_tail = (parent->accept_tail + 1) % TCP_ACCEPT_QUEUE;
                    parent->accept_count++;
                    wake_process(&parent->blocked_accepter);
                }
            }
        }
        break;

    case TCP_SYN_SENT:
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            c->rcv_nxt = seq + 1;
            c->snd_una = ack;
            c->state = TCP_ESTABLISHED;
            /* Send ACK */
            tcp_send_segment(idx, TCP_ACK, NULL, 0);
            wake_process(&c->blocked_writer);
        }
        break;

    case TCP_ESTABLISHED:
        /* Update snd_una */
        if (flags & TCP_ACK)
            c->snd_una = ack;

        /* Process incoming data */
        if (data_len > 0 && seq == c->rcv_nxt) {
            uint32_t space = TCP_RX_BUF_SIZE - c->rx_count;
            uint32_t copy = data_len < space ? data_len : space;
            for (uint32_t i = 0; i < copy; i++) {
                c->rx_buf[c->rx_write_pos] = data[i];
                c->rx_write_pos = (c->rx_write_pos + 1) % TCP_RX_BUF_SIZE;
            }
            c->rx_count += copy;
            c->rcv_nxt += copy;
            /* Send ACK */
            tcp_send_segment(idx, TCP_ACK, NULL, 0);
            wake_process(&c->blocked_reader);
        }

        /* Handle FIN */
        if (flags & TCP_FIN) {
            c->rcv_nxt = seq + data_len + 1;
            c->state = TCP_CLOSE_WAIT;
            c->eof = true;
            tcp_send_segment(idx, TCP_ACK, NULL, 0);
            wake_process(&c->blocked_reader);
        }

        /* Handle RST */
        if (flags & TCP_RST) {
            c->state = TCP_CLOSED;
            c->eof = true;
            wake_process(&c->blocked_reader);
            wake_process(&c->blocked_writer);
        }
        break;

    case TCP_FIN_WAIT_1:
        if (flags & TCP_ACK) {
            c->snd_una = ack;
            c->state = TCP_FIN_WAIT_2;
        }
        if (flags & TCP_FIN) {
            c->rcv_nxt = seq + 1;
            c->state = TCP_TIME_WAIT;
            tcp_send_segment(idx, TCP_ACK, NULL, 0);
        }
        break;

    case TCP_FIN_WAIT_2:
        if (flags & TCP_FIN) {
            c->rcv_nxt = seq + 1;
            c->state = TCP_TIME_WAIT;
            tcp_send_segment(idx, TCP_ACK, NULL, 0);
        }
        break;

    case TCP_LAST_ACK:
        if (flags & TCP_ACK) {
            c->state = TCP_CLOSED;
            c->in_use = false;
        }
        break;

    default:
        break;
    }

    netbuf_free(buf);
}

void tcp_tick(void) {
    /* Simple retransmit timer */
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!conns[i].in_use)
            continue;

        /* Retransmit check */
        if (conns[i].retransmit_len > 0 && conns[i].retransmit_timer > 0) {
            conns[i].retransmit_timer--;
            if (conns[i].retransmit_timer == 0) {
                if (conns[i].retransmit_count < 3) {
                    /* Resend */
                    uint32_t saved_nxt = conns[i].snd_nxt;
                    conns[i].snd_nxt = conns[i].retransmit_seq;
                    tcp_send_segment(i, TCP_ACK | TCP_PSH,
                                     conns[i].retransmit_data,
                                     conns[i].retransmit_len);
                    conns[i].snd_nxt = saved_nxt;
                    conns[i].retransmit_timer = 50; /* ~500ms at 100Hz */
                    conns[i].retransmit_count++;
                } else {
                    /* Give up, RST */
                    conns[i].retransmit_len = 0;
                    tcp_send_segment(i, TCP_RST, NULL, 0);
                    conns[i].state = TCP_CLOSED;
                    wake_process(&conns[i].blocked_reader);
                    wake_process(&conns[i].blocked_writer);
                }
            }
        }

        /* TIME_WAIT cleanup (after ~2 seconds) */
        if (conns[i].state == TCP_TIME_WAIT) {
            if (conns[i].retransmit_timer == 0)
                conns[i].retransmit_timer = 200; /* 2 seconds */
            conns[i].retransmit_timer--;
            if (conns[i].retransmit_timer == 0) {
                conns[i].state = TCP_CLOSED;
                conns[i].in_use = false;
            }
        }
    }
}

int tcp_connect(int idx, uint32_t dst_ip, uint16_t dst_port, uint16_t src_port) {
    struct tcp_connection *c = &conns[idx];
    c->local_ip = net_get_ip();
    c->local_port = src_port;
    c->remote_ip = dst_ip;
    c->remote_port = dst_port;
    c->snd_nxt = pit_get_ticks() * 54321 + 1;
    c->snd_una = c->snd_nxt;
    c->state = TCP_SYN_SENT;

    /* Send SYN */
    tcp_send_segment(idx, TCP_SYN, NULL, 0);

    /* Block until ESTABLISHED or timeout */
    struct process *cur = scheduler_get_current();
    c->blocked_writer = cur;
    cur->state = PROCESS_BLOCKED;
    schedule();
    sti();

    if (c->state != TCP_ESTABLISHED)
        return -1;
    return 0;
}

int tcp_listen(int idx, uint16_t port) {
    struct tcp_connection *c = &conns[idx];
    c->local_ip = net_get_ip();
    c->local_port = port;
    c->state = TCP_LISTEN;
    c->accept_head = 0;
    c->accept_tail = 0;
    c->accept_count = 0;
    return 0;
}

int tcp_accept(int idx) {
    struct tcp_connection *c = &conns[idx];
    if (c->state != TCP_LISTEN)
        return -1;

    while (c->accept_count == 0) {
        struct process *cur = scheduler_get_current();
        if (signal_has_fatal(cur))
            return -1;
        c->blocked_accepter = cur;
        cur->state = PROCESS_BLOCKED;
        schedule();
        sti();
    }

    int new_idx = c->accept_queue[c->accept_head];
    c->accept_head = (c->accept_head + 1) % TCP_ACCEPT_QUEUE;
    c->accept_count--;
    return new_idx;
}

int tcp_send(int idx, const uint8_t *data, uint32_t len) {
    struct tcp_connection *c = &conns[idx];
    if (c->state != TCP_ESTABLISHED && c->state != TCP_CLOSE_WAIT)
        return -1;

    /* Send in chunks up to 1460 bytes (MSS) */
    uint32_t sent = 0;
    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > 1460) chunk = 1460;

        /* Save for retransmit */
        c->retransmit_seq = c->snd_nxt;
        memcpy(c->retransmit_data, data + sent, chunk);
        c->retransmit_len = chunk;
        c->retransmit_timer = 50; /* 500ms */
        c->retransmit_count = 0;

        tcp_send_segment(idx, TCP_ACK | TCP_PSH, data + sent, chunk);
        sent += chunk;
    }

    return (int)sent;
}

int tcp_recv(int idx, uint8_t *buf, uint32_t len) {
    struct tcp_connection *c = &conns[idx];

    /* Block until data available or EOF */
    while (c->rx_count == 0 && !c->eof &&
           c->state != TCP_CLOSED && c->state != TCP_CLOSE_WAIT) {
        struct process *cur = scheduler_get_current();
        if (signal_has_fatal(cur))
            return -1;
        c->blocked_reader = cur;
        cur->state = PROCESS_BLOCKED;
        schedule();
        sti();
    }

    if (c->rx_count == 0) {
        /* EOF or closed */
        return 0;
    }

    uint32_t copy = len < c->rx_count ? len : c->rx_count;
    for (uint32_t i = 0; i < copy; i++) {
        buf[i] = c->rx_buf[c->rx_read_pos];
        c->rx_read_pos = (c->rx_read_pos + 1) % TCP_RX_BUF_SIZE;
    }
    c->rx_count -= copy;
    return (int)copy;
}

int tcp_close(int idx) {
    struct tcp_connection *c = &conns[idx];

    if (c->state == TCP_ESTABLISHED) {
        c->state = TCP_FIN_WAIT_1;
        tcp_send_segment(idx, TCP_FIN | TCP_ACK, NULL, 0);
    } else if (c->state == TCP_CLOSE_WAIT) {
        c->state = TCP_LAST_ACK;
        tcp_send_segment(idx, TCP_FIN | TCP_ACK, NULL, 0);
    } else if (c->state == TCP_LISTEN || c->state == TCP_SYN_SENT) {
        c->state = TCP_CLOSED;
        c->in_use = false;
    }

    return 0;
}

bool tcp_conn_readable(int idx) {
    struct tcp_connection *c = tcp_get_conn(idx);
    if (!c) return false;
    return c->rx_count > 0 || c->eof || c->state == TCP_CLOSED;
}

bool tcp_conn_writable(int idx) {
    struct tcp_connection *c = tcp_get_conn(idx);
    if (!c) return false;
    return c->state == TCP_ESTABLISHED;
}

bool tcp_conn_acceptable(int idx) {
    struct tcp_connection *c = tcp_get_conn(idx);
    if (!c) return false;
    return c->state == TCP_LISTEN && c->accept_count > 0;
}
