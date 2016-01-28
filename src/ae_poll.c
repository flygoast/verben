/* poll(2) based ae.c module */
#include <poll.h>
#include <string.h>

typedef struct ae_api_state {
    int nfds;
    struct pollfd events[AE_SETSIZE];
} ae_api_state;

static int ae_api_create(ae_event_loop *el) {
    ae_api_state *state = calloc(1, sizeof(ae_api_state));

    if (!state) {
        return -1;
    }

    memset(state->events, 0, sizeof(struct pollfd) * AE_SETSIZE);
    el->api_data = state;
    return 0;
}

static void ae_api_free(ae_event_loop *el) {
    if (el && el->api_data) {
        free(el->api_data);
    }
}

static int ae_api_add_event(ae_event_loop *el, int fd, int mask) {
    ae_api_state *state = el->api_data;
    int i = 0;
    struct pollfd *pfd = NULL;
    for (i = 0; i < state->nfds; i++) {
        if (fd == state->events[i].fd) {
            pfd = &state->events[i];
        }
    }

    if (!pfd) {
        pfd = &state->events[state->nfds++];
    }

    if (mask & AE_READABLE) {
        pfd->fd = fd;
        pfd->events |= POLLIN;
    }

    if (mask & AE_WRITABLE) {
        pfd->fd = fd;
        pfd->events |= POLLOUT;
    }
    return 0;
}

static void ae_api_del_event(ae_event_loop *el, int fd, int mask) {
    ae_api_state *state = el->api_data;
    int i = 0;
    struct pollfd *pfd = NULL;
    for (i = 0; i < state->nfds; i++) {
        if (fd == state->events[i].fd) {
            pfd = &state->events[i];
        }
    }

    if (!pfd) {
        return;
    }

    if (mask & AE_READABLE) {
        pfd->events &= ~POLLIN;
    }

    if (mask & AE_WRITABLE) {
        pfd->events &= ~POLLOUT;
    }

    if (pfd->events == 0) {
        pfd->fd = -1; /* ignore the element of pollfd array */
    }
}

static int ae_api_poll(ae_event_loop *el, struct timeval *tvp) {
    ae_api_state *state = el->api_data;
    int retval, numevents = 0;
    retval = poll(state->events, state->nfds, 
            tvp ? (tvp->tv_sec * 1000 + tvp->tv_usec / 1000) : -1);
    if (retval > 0) {
        int i = 0, n = 0;
        numevents = retval;
        for (i = 0; i < state->nfds; ++i) {
            int mask = 0;
            struct pollfd *pfd = state->events + i;
            if (pfd->fd < 0) {
                continue; /* Skip the ignored element. */
            }

            if (pfd->revents == 0) {
                continue;
            }

            if (pfd->revents & POLLIN) {
                mask |= AE_READABLE;
            }

            if (pfd->revents & POLLOUT) {
                mask |= AE_WRITABLE;
            }
            el->fired[n].fd = pfd->fd;
            el->fired[n++].mask = mask;
        }
    }
    return numevents;
}

static char *ae_api_name(void) {
    return "poll";
}
