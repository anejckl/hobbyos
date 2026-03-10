#ifndef EPOLL_H
#define EPOLL_H

#include "../common.h"

#define EPOLLIN   0x001
#define EPOLLOUT  0x004
#define EPOLLERR  0x008
#define EPOLLHUP  0x010

#define EPOLL_CTL_ADD  1
#define EPOLL_CTL_MOD  2
#define EPOLL_CTL_DEL  3

#define EPOLL_MAX_INSTANCES  16
#define EPOLL_MAX_WATCHED    32

struct epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));

struct epoll_watch {
    int fd;
    uint32_t events;
    uint64_t data;
};

struct epoll_instance {
    bool in_use;
    uint32_t owner_pid;
    struct epoll_watch watches[EPOLL_MAX_WATCHED];
    int watch_count;
};

/* Create an epoll instance. Returns index (0+), or -1 on error. */
int epoll_create_instance(uint32_t owner_pid);

/* Add/modify/remove a watched fd. Returns 0 or -1. */
int epoll_ctl_instance(int epfd_idx, int op, int fd, uint32_t events, uint64_t data);

/* Get the epoll_instance by index. */
struct epoll_instance *epoll_get(int idx);

/* Free an epoll instance. */
void epoll_destroy(int idx);

#endif /* EPOLL_H */
