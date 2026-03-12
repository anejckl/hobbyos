#ifndef SOCKET_H
#define SOCKET_H

#include "../common.h"

#define MAX_SOCKETS     32
#define SOCK_DGRAM      1
#define SOCK_STREAM     2
#define AF_INET         2

#define SOCKET_UDP_QUEUE_SIZE 8
#define SOCKET_UDP_PKT_SIZE   1472

struct process; /* forward declaration */

struct socket_udp_pkt {
    uint8_t data[SOCKET_UDP_PKT_SIZE];
    uint32_t len;
    uint32_t src_ip;
    uint16_t src_port;
    bool valid;
};

/* fcntl commands */
#define F_GETFL  3
#define F_SETFL  4
#define O_NONBLOCK  0x800

/* Error codes */
#define EAGAIN      11
#define EINPROGRESS 115

struct socket {
    int type;           /* SOCK_DGRAM or SOCK_STREAM */
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    bool bound;
    bool listening;
    bool connected;
    bool in_use;
    bool nonblocking;   /* O_NONBLOCK set via fcntl */
    int refcount;

    /* UDP rx queue */
    struct socket_udp_pkt udp_queue[SOCKET_UDP_QUEUE_SIZE];
    int udp_head;
    int udp_tail;
    int udp_count;

    /* TCP connection index (-1 if none) */
    int tcp_conn_idx;

    /* Blocked waiters */
    struct process *blocked_reader;
    struct process *blocked_writer;
};

int socket_create(int type);
int socket_bind(int sock_idx, uint32_t ip, uint16_t port);
int socket_listen(int sock_idx, int backlog);
int socket_accept(int sock_idx);
int socket_connect(int sock_idx, uint32_t ip, uint16_t port);
int socket_send(int sock_idx, const uint8_t *data, uint32_t len);
int socket_recv(int sock_idx, uint8_t *buf, uint32_t len);
int socket_close(int sock_idx);
void socket_inc_ref(int sock_idx);
struct socket *socket_get(int sock_idx);
bool socket_readable(int sock_idx);
bool socket_writable(int sock_idx);

/* Called by UDP layer to deliver packets */
void socket_udp_deliver(uint16_t dst_port, uint32_t src_ip, uint16_t src_port,
                         const uint8_t *data, uint32_t len);

#endif /* SOCKET_H */
