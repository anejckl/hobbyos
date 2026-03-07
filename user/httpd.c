#include "syscall.h"
#include "ulib.h"

static const char *http_response =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><body><h1>HobbyOS</h1><p>Hello from the hobby kernel!</p></body></html>\r\n";

void _start(void) {
    int sock = (int)sys_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { print("httpd: socket failed\n"); sys_exit(1); }
    if (sys_bind(sock, 0, 80) < 0) { print("httpd: bind failed\n"); sys_exit(1); }
    if (sys_listen(sock, 4) < 0) { print("httpd: listen failed\n"); sys_exit(1); }

    print("httpd: listening on port 80\n");

    while (1) {
        int client = (int)sys_accept(sock);
        if (client < 0) continue;

        /* Read request (we don't parse it, just drain) */
        char buf[512];
        sys_read(client, buf, sizeof(buf));

        /* Send response */
        sys_write(client, http_response, ulib_strlen(http_response));

        sys_close(client);
    }
}
