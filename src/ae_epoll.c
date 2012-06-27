/* Linux epoll(2) based ae.c module. */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>

typedef struct ae_api_state {
    int epfd;
    struct epoll_event events[AE_SETSIZE];
} ae_api_state;

static int ae_api_create(ae_event_loop *el) {
    ae_api_state *state = malloc(sizeof(*state));
    if (!state) {
        return -1;
    }

    /* 1024 is just a hint for the kernel */
    state->epfd = epoll_create(1024); 
    if (state->epfd == -1) {
        return -1;
    }
    el->api_data = state;
    return 0;
}

static void ae_api_free(ae_event_loop *el) {
    ae_api_state *state = el->api_data;
    close(state->epfd);
    free(state);
}

static int ae_api_add_event(ae_event_loop *el, int fd, int mask) {
    ae_api_state *state = el->api_data;
    struct epoll_event ee;
    /* If the fd was already monitored for some event, we need a MOD
       operation. Otherwise we need an ADD operation. */
    int op = el->events[fd].mask == AE_NONE ? 
        EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    ee.events = 0;
    mask |= el->events[fd].mask; /* Merge old events. */
    if (mask & AE_READABLE) {
        ee.events |= EPOLLIN;
    }

    if (mask & AE_WRITABLE) {
        ee.events |= EPOLLOUT;
    }
    ee.data.u64 = 0;
    ee.data.fd = fd;
    if (epoll_ctl(state->epfd, op, fd, &ee) == -1) {
        fprintf(stderr, "%s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static void ae_api_del_event(ae_event_loop *el, int fd, int delmask) {
    ae_api_state *state = el->api_data;
    struct epoll_event ee;
    int mask = el->events[fd].mask & (~delmask);
    ee.events = 0;
    if (mask & AE_READABLE) {
        ee.events |= EPOLLIN;
    }

    if (mask & AE_WRITABLE) {
        ee.events |= EPOLLOUT;
    }
    ee.data.u64 = 0;
    ee.data.fd = fd;
    if (mask != AE_NONE) {
        epoll_ctl(state->epfd, EPOLL_CTL_MOD, fd, &ee);
    } else {
        /* Note, kernel < 2.6.9 requires a non null event pointer even
           for EPOLL_CTL_DEL. */
        epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, &ee);
    }
}

static int ae_api_poll(ae_event_loop *el, struct timeval *tvp) {
    ae_api_state *state = el->api_data;
    int retval, numevents = 0;
    retval = epoll_wait(state->epfd, state->events, AE_SETSIZE,
        tvp ? (tvp->tv_sec * 1000 + tvp->tv_usec / 1000) : -1);
    if (retval > 0) {
        int j;
        numevents = retval;
        for (j = 0; j < numevents; ++j) {
            int mask = 0;
            struct epoll_event *e = state->events + j;
            if (e->events & EPOLLIN) {
                mask |= AE_READABLE;
            }

            if (e->events & EPOLLOUT) {
                mask |= AE_WRITABLE;
            }
            el->fired[j].fd = e->data.fd;
            el->fired[j].mask = mask;
        }
    }
    return numevents;
}

static char *ae_api_name(void) {
    return "epoll";
}
