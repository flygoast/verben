/* Kqueue(2) based ae.c module */
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

typedef struct ae_api_state {
    int kqfd;
    struct kevent events[AE_SETSIZE];
} ae_api_state;

static int ae_api_create(ae_event_loop *el) {
    ae_api_state *state = malloc(sizeof(*state));
    if (!state) {
        return -1;
    }
    state->kqfd = kqueue();
    if (state->kqfd == -1) {
        return -1;
    }
    el->api_data = state;
    return 0;
}

static void ae_api_free(ae_event_loop *el) {
    ae_api_state *state = el->api_data;
    close(state->kqfd);
    free(state);
}

static int ae_api_add_event(ae_event_loop *el, int fd, int mask) {
    ae_api_state *state = el->api_data;
    struct kevent ke;

    if (mask & AE_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
        if (kevent(state->kqfd, &ke, 1, NULL, 0, NULL) == -1) {
            return -1;
        }
    }

    if (mask & AE_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
        if (kevent(state->kqfd, &ke, 1, NULL, 0, NULL) == -1) {
            return -1;
        }
    }
    return 0;
}

static void ae_api_del_event(ae_event_loop *el, int fd, int mask) {
    ae_api_state *state = el->api_data;
    struct kevent ke;
    if (mask & AE_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(state->kqfd, &ke, 1, NULL, 0, NULL);
    }

    if (mask & AE_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(state->kqfd, &ke, 1, NULL, 0, NULL);
    }
}

static int ae_api_poll(ae_event_loop *el, struct timeval *tvp) {
    ae_api_state *state = el->api_data;
    int retval, numevents = 0;
    if (tvp != NULL) {
        struct timespec timeout;
        timeout.tv_sec = tvp->tv_sec;
        timeout.tv_nsec = tvp->tv_usec * 1000;
        retval = kevent(state->kqfd, NULL, 0, 
                state->events, AE_SETSIZE, &timeout);
    } else {
        retval = kevent(state->kqfd, NULL, 0, state->events, 
                AE_SETSIZE, NULL);
    }

    if (retval > 0) {
        int j;
        numevents = retval;
        for (j = 0; j < numevents; ++j) {
            int mask = 0;
            struct kevent *e = state->events + j;
            if (e->filter == EVFILT_READ) {
                mask |= AE_READABLE;
            }
            if (e->filter == EVFILT_WRITE) {
                mask |= AE_WRITABLE;
            }
            el->fired[j].fd = e->ident;
            el->fired[j].mask = mask;
        }
    }
    return numevents;
}

static char *ae_api_name(void) {
    return "kqueue";
}
