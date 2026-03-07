#ifndef TCP_H
#define TCP_H

#include "../common.h"
#include "netbuf.h"

#define TCP_MAX_CONNS    32
#define TCP_RX_BUF_SIZE  4096
#define TCP_ACCEPT_QUEUE 4
#define TCP_WINDOW_SIZE  4096

/* TCP flags */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset; /* upper 4 bits = header length in 32-bit words */
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

enum tcp_state {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
};

struct process; /* forward declaration */

struct tcp_connection {
    enum tcp_state state;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;

    uint32_t snd_nxt;    /* next seq to send */
    uint32_t snd_una;    /* oldest unacked seq */
    uint32_t rcv_nxt;    /* next expected seq from remote */

    /* Receive buffer (ring) */
    uint8_t rx_buf[TCP_RX_BUF_SIZE];
    uint32_t rx_read_pos;
    uint32_t rx_write_pos;
    uint32_t rx_count;

    /* Blocked waiters */
    struct process *blocked_reader;
    struct process *blocked_writer;
    struct process *blocked_accepter;

    /* Accept queue for listening sockets */
    int accept_queue[TCP_ACCEPT_QUEUE];
    int accept_head;
    int accept_tail;
    int accept_count;
    int parent_idx;  /* index of listening connection, -1 if none */

    /* Retransmit state */
    uint8_t retransmit_data[1460];
    uint32_t retransmit_len;
    uint32_t retransmit_seq;
    uint32_t retransmit_timer;
    uint32_t retransmit_count;

    bool in_use;
    bool eof;        /* FIN received */
};

void tcp_init(void);
void tcp_receive(struct netbuf *buf, uint32_t src_ip, uint32_t tcp_len);
void tcp_tick(void);

/* Socket-layer API */
int tcp_alloc_conn(void);
int tcp_connect(int idx, uint32_t dst_ip, uint16_t dst_port, uint16_t src_port);
int tcp_listen(int idx, uint16_t port);
int tcp_accept(int idx);
int tcp_send(int idx, const uint8_t *data, uint32_t len);
int tcp_recv(int idx, uint8_t *buf, uint32_t len);
int tcp_close(int idx);
struct tcp_connection *tcp_get_conn(int idx);
bool tcp_conn_readable(int idx);
bool tcp_conn_writable(int idx);
bool tcp_conn_acceptable(int idx);

#endif /* TCP_H */
