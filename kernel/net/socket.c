#include "socket.h"
#include "net.h"
#include "udp.h"
#include "tcp.h"
#include "../string.h"
#include "../scheduler/scheduler.h"
#include "../process/process.h"
#include "../debug/debug.h"
#include "../signal/signal.h"

static struct socket sockets[MAX_SOCKETS];
static uint16_t next_ephemeral_port = 49152;

int socket_create(int type) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i].in_use) {
            memset(&sockets[i], 0, sizeof(sockets[i]));
            sockets[i].type = type;
            sockets[i].in_use = true;
            sockets[i].refcount = 1;
            sockets[i].tcp_conn_idx = -1;
            return i;
        }
    }
    return -1;
}

struct socket *socket_get(int sock_idx) {
    if (sock_idx < 0 || sock_idx >= MAX_SOCKETS || !sockets[sock_idx].in_use)
        return NULL;
    return &sockets[sock_idx];
}

int socket_bind(int sock_idx, uint32_t ip, uint16_t port) {
    struct socket *s = socket_get(sock_idx);
    if (!s) return -1;

    s->local_ip = ip ? ip : net_get_ip();
    s->local_port = port;
    s->bound = true;
    return 0;
}

int socket_listen(int sock_idx, int backlog) {
    (void)backlog;
    struct socket *s = socket_get(sock_idx);
    if (!s || s->type != SOCK_STREAM || !s->bound)
        return -1;

    /* Allocate TCP connection for listening */
    int tcp_idx = tcp_alloc_conn();
    if (tcp_idx < 0) return -1;

    tcp_listen(tcp_idx, s->local_port);
    s->tcp_conn_idx = tcp_idx;
    s->listening = true;
    return 0;
}

int socket_accept(int sock_idx) {
    struct socket *s = socket_get(sock_idx);
    if (!s || !s->listening)
        return -1;

    /* Block until a connection is in accept queue */
    int new_tcp_idx = tcp_accept(s->tcp_conn_idx);
    if (new_tcp_idx < 0)
        return -1;

    /* Create new socket for accepted connection */
    int new_sock = socket_create(SOCK_STREAM);
    if (new_sock < 0)
        return -1;

    struct socket *ns = &sockets[new_sock];
    struct tcp_connection *tc = tcp_get_conn(new_tcp_idx);
    ns->tcp_conn_idx = new_tcp_idx;
    ns->connected = true;
    ns->bound = true;
    if (tc) {
        ns->local_ip = tc->local_ip;
        ns->local_port = tc->local_port;
        ns->remote_ip = tc->remote_ip;
        ns->remote_port = tc->remote_port;
    }
    return new_sock;
}

int socket_connect(int sock_idx, uint32_t ip, uint16_t port) {
    struct socket *s = socket_get(sock_idx);
    if (!s) return -1;

    if (s->type == SOCK_STREAM) {
        int tcp_idx = tcp_alloc_conn();
        if (tcp_idx < 0) return -1;

        uint16_t src_port = s->bound ? s->local_port : next_ephemeral_port++;
        if (!s->bound) {
            s->local_port = src_port;
            s->bound = true;
        }

        s->tcp_conn_idx = tcp_idx;
        s->remote_ip = ip;
        s->remote_port = port;

        if (tcp_connect(tcp_idx, ip, port, src_port) < 0)
            return -1;

        s->connected = true;
        return 0;
    } else if (s->type == SOCK_DGRAM) {
        s->remote_ip = ip;
        s->remote_port = port;
        s->connected = true;
        if (!s->bound) {
            s->local_port = next_ephemeral_port++;
            s->bound = true;
        }
        return 0;
    }
    return -1;
}

int socket_send(int sock_idx, const uint8_t *data, uint32_t len) {
    struct socket *s = socket_get(sock_idx);
    if (!s) return -1;

    if (s->type == SOCK_STREAM) {
        if (s->tcp_conn_idx < 0) return -1;
        return tcp_send(s->tcp_conn_idx, data, len);
    } else if (s->type == SOCK_DGRAM) {
        if (!s->connected) return -1;
        return udp_send(s->remote_ip, s->local_port, s->remote_port, data, len);
    }
    return -1;
}

int socket_recv(int sock_idx, uint8_t *buf, uint32_t len) {
    struct socket *s = socket_get(sock_idx);
    if (!s) return -1;

    if (s->type == SOCK_STREAM) {
        if (s->tcp_conn_idx < 0) return -1;
        return tcp_recv(s->tcp_conn_idx, buf, len);
    } else if (s->type == SOCK_DGRAM) {
        /* Block until packet available */
        while (s->udp_count == 0) {
            struct process *cur = scheduler_get_current();
            if (signal_has_fatal(cur))
                return -1;
            s->blocked_reader = cur;
            cur->state = PROCESS_BLOCKED;
            schedule();
            sti();
        }

        struct socket_udp_pkt *pkt = &s->udp_queue[s->udp_head];
        uint32_t copy = len < pkt->len ? len : pkt->len;
        memcpy(buf, pkt->data, copy);
        pkt->valid = false;
        s->udp_head = (s->udp_head + 1) % SOCKET_UDP_QUEUE_SIZE;
        s->udp_count--;
        return (int)copy;
    }
    return -1;
}

int socket_close(int sock_idx) {
    struct socket *s = socket_get(sock_idx);
    if (!s) return -1;

    s->refcount--;
    if (s->refcount > 0)
        return 0;

    if (s->type == SOCK_STREAM && s->tcp_conn_idx >= 0)
        tcp_close(s->tcp_conn_idx);

    s->in_use = false;
    return 0;
}

void socket_inc_ref(int sock_idx) {
    struct socket *s = socket_get(sock_idx);
    if (s)
        s->refcount++;
}

bool socket_readable(int sock_idx) {
    struct socket *s = socket_get(sock_idx);
    if (!s) return false;

    if (s->type == SOCK_STREAM) {
        if (s->listening)
            return tcp_conn_acceptable(s->tcp_conn_idx);
        return s->tcp_conn_idx >= 0 && tcp_conn_readable(s->tcp_conn_idx);
    } else if (s->type == SOCK_DGRAM) {
        return s->udp_count > 0;
    }
    return false;
}

bool socket_writable(int sock_idx) {
    struct socket *s = socket_get(sock_idx);
    if (!s) return false;

    if (s->type == SOCK_STREAM)
        return s->tcp_conn_idx >= 0 && tcp_conn_writable(s->tcp_conn_idx);
    return true; /* UDP is always writable */
}

void socket_udp_deliver(uint16_t dst_port, uint32_t src_ip, uint16_t src_port,
                         const uint8_t *data, uint32_t len) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].in_use &&
            sockets[i].type == SOCK_DGRAM &&
            sockets[i].bound &&
            sockets[i].local_port == dst_port) {

            struct socket *s = &sockets[i];
            if (s->udp_count < SOCKET_UDP_QUEUE_SIZE) {
                struct socket_udp_pkt *pkt = &s->udp_queue[s->udp_tail];
                uint32_t copy = len < SOCKET_UDP_PKT_SIZE ? len : SOCKET_UDP_PKT_SIZE;
                memcpy(pkt->data, data, copy);
                pkt->len = copy;
                pkt->src_ip = src_ip;
                pkt->src_port = src_port;
                pkt->valid = true;
                s->udp_tail = (s->udp_tail + 1) % SOCKET_UDP_QUEUE_SIZE;
                s->udp_count++;

                /* Wake reader */
                if (s->blocked_reader) {
                    s->blocked_reader->state = PROCESS_READY;
                    scheduler_add(s->blocked_reader);
                    s->blocked_reader = NULL;
                }
            }
            return;
        }
    }
}
