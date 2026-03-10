#include "epoll.h"
#include "../string.h"
#include "../debug/debug.h"

static struct epoll_instance epoll_pool[EPOLL_MAX_INSTANCES];

int epoll_create_instance(uint32_t owner_pid) {
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (!epoll_pool[i].in_use) {
            memset(&epoll_pool[i], 0, sizeof(epoll_pool[i]));
            epoll_pool[i].in_use = true;
            epoll_pool[i].owner_pid = owner_pid;
            return i;
        }
    }
    return -1;
}

int epoll_ctl_instance(int epfd_idx, int op, int fd, uint32_t events, uint64_t data) {
    if (epfd_idx < 0 || epfd_idx >= EPOLL_MAX_INSTANCES) return -1;
    struct epoll_instance *ep = &epoll_pool[epfd_idx];
    if (!ep->in_use) return -1;

    if (op == EPOLL_CTL_ADD) {
        if (ep->watch_count >= EPOLL_MAX_WATCHED) return -1;
        ep->watches[ep->watch_count].fd = fd;
        ep->watches[ep->watch_count].events = events;
        ep->watches[ep->watch_count].data = data;
        ep->watch_count++;
        return 0;
    } else if (op == EPOLL_CTL_MOD) {
        for (int i = 0; i < ep->watch_count; i++) {
            if (ep->watches[i].fd == fd) {
                ep->watches[i].events = events;
                ep->watches[i].data = data;
                return 0;
            }
        }
        return -1;
    } else if (op == EPOLL_CTL_DEL) {
        for (int i = 0; i < ep->watch_count; i++) {
            if (ep->watches[i].fd == fd) {
                /* Compact */
                for (int j = i; j < ep->watch_count - 1; j++)
                    ep->watches[j] = ep->watches[j+1];
                ep->watch_count--;
                return 0;
            }
        }
        return -1;
    }
    return -1;
}

struct epoll_instance *epoll_get(int idx) {
    if (idx < 0 || idx >= EPOLL_MAX_INSTANCES) return NULL;
    if (!epoll_pool[idx].in_use) return NULL;
    return &epoll_pool[idx];
}

void epoll_destroy(int idx) {
    if (idx >= 0 && idx < EPOLL_MAX_INSTANCES)
        epoll_pool[idx].in_use = false;
}
