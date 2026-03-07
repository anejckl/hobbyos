#include "syscall.h"
#include "ulib.h"

/* Parse dotted-decimal IP string to uint32_t (host byte order) */
static uint32_t parse_ip(const char *s) {
    uint32_t octets[4] = {0, 0, 0, 0};
    int part = 0;
    while (*s && part < 4) {
        if (*s >= '0' && *s <= '9')
            octets[part] = octets[part] * 10 + (uint32_t)(*s - '0');
        else if (*s == '.')
            part++;
        s++;
    }
    return (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
}

static uint16_t parse_port(const char *s) {
    uint16_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (uint16_t)(*s - '0');
        s++;
    }
    return val;
}

/* Simple tokenizer */
static const char *skip_spaces(const char *s) {
    while (*s == ' ') s++;
    return s;
}

static const char *next_token(const char *s) {
    while (*s && *s != ' ') s++;
    return skip_spaces(s);
}

void _start(void) {
    const char *args = get_argv();
    if (!args) {
        print("Usage: nc [-l port] [ip port]\n");
        sys_exit(1);
    }

    args = skip_spaces(args);

    if (args[0] == '-' && args[1] == 'l') {
        /* Server mode: nc -l <port> */
        const char *port_str = next_token(args);
        uint16_t port = parse_port(port_str);

        int sock = (int)sys_socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { print("nc: socket failed\n"); sys_exit(1); }
        if (sys_bind(sock, 0, port) < 0) { print("nc: bind failed\n"); sys_exit(1); }
        if (sys_listen(sock, 4) < 0) { print("nc: listen failed\n"); sys_exit(1); }

        print("nc: listening on port ");
        print_num((uint64_t)port);
        print("\n");

        int client = (int)sys_accept(sock);
        if (client < 0) { print("nc: accept failed\n"); sys_exit(1); }
        print("nc: connection accepted\n");

        /* Multiplex stdin and socket using select */
        char buf[512];
        while (1) {
            struct select_args sa;
            sa.readfds = (1U << 0) | (1U << (uint32_t)client);
            sa.writefds = 0;
            sa.exceptfds = 0;
            sa.timeout_ms = 1000;

            int ret = (int)sys_select((uint32_t)client + 1, &sa);
            if (ret <= 0) continue;

            if (sa.readfds & (1U << (uint32_t)client)) {
                int n = (int)sys_read(client, buf, sizeof(buf));
                if (n <= 0) break;
                sys_write(1, buf, (uint64_t)n);
            }
            if (sa.readfds & (1U << 0)) {
                int n = (int)sys_read(0, buf, sizeof(buf));
                if (n <= 0) break;
                sys_write(client, buf, (uint64_t)n);
            }
        }

        sys_close(client);
        sys_close(sock);
    } else {
        /* Client mode: nc <ip> <port> */
        uint32_t ip = parse_ip(args);
        const char *port_str = next_token(args);
        uint16_t port = parse_port(port_str);

        int sock = (int)sys_socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { print("nc: socket failed\n"); sys_exit(1); }
        if (sys_connect(sock, ip, port) < 0) { print("nc: connect failed\n"); sys_exit(1); }
        print("nc: connected\n");

        char buf[512];
        while (1) {
            struct select_args sa;
            sa.readfds = (1U << 0) | (1U << (uint32_t)sock);
            sa.writefds = 0;
            sa.exceptfds = 0;
            sa.timeout_ms = 1000;

            int ret = (int)sys_select((uint32_t)sock + 1, &sa);
            if (ret <= 0) continue;

            if (sa.readfds & (1U << (uint32_t)sock)) {
                int n = (int)sys_read(sock, buf, sizeof(buf));
                if (n <= 0) break;
                sys_write(1, buf, (uint64_t)n);
            }
            if (sa.readfds & (1U << 0)) {
                int n = (int)sys_read(0, buf, sizeof(buf));
                if (n <= 0) break;
                sys_write(sock, buf, (uint64_t)n);
            }
        }

        sys_close(sock);
    }

    sys_exit(0);
}
