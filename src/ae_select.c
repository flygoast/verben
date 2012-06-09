/* select(2) based ae.c module. */
#include <string.h>

typedef struct ae_api_state {
    fd_set rfds, wfds;
    /* We need to have a copy of the fd sets as it's not safe to
       reuse FD sets after select(). */
    fd_set _rfds, _wfds;
} ae_api_state;

static int ae_api_create(ae_event_loop *el) {
    ae_api_state *state = malloc(sizeof(*state));
    if (!state) {
        return -1;
    }
    FD_ZERO(&state->rfds);
    FD_ZERO(&state->wfds);
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

    if (mask & AE_READABLE) {
        FD_SET(fd, &state->rfds);
    }

    if (mask & AE_WRITABLE) {
        FD_SET(fd, &state->wfds);
    }
    return 0;
}

static void ae_api_del_event(ae_event_loop *el, int fd, int mask) {
    ae_api_state *state = el->api_data;
    if (mask & AE_READABLE) {
        FD_CLR(fd, &state->rfds);
    }

    if (mask & AE_WRITABLE) {
        FD_CLR(fd, &state->wfds);
    }
}

static int ae_api_poll(ae_event_loop *el, struct timeval *tvp) {
    ae_api_state *state = el->api_data;
    int ret, j, numevents = 0;
    memcpy(&state->_rfds, &state->rfds, sizeof(fd_set));
    memcpy(&state->_wfds, &state->wfds, sizeof(fd_set));

    ret = select(el->maxfd + 1, &state->_rfds, &state->_wfds, NULL, tvp);
    if (ret > 0) {
        for (j = 0; j <= el->maxfd; ++j) {
            int mask = 0;
            ae_file_event *fe = &el->events[j];
            if (fe->mask == AE_NONE) {
                continue;
            }
            if (fe->mask & AE_READABLE && FD_ISSET(j, &state->_rfds)) {
                mask |= AE_READABLE;
            }
            if (fe->mask & AE_WRITABLE && FD_ISSET(j, &state->_wfds)) {
                mask |= AE_WRITABLE;
            }
            el->fired[numevents].fd = j;
            el->fired[numevents].mask = mask;
            numevents++;
        }
    }
    return numevents;
}

static char *ae_api_name(void) {
    return "select";
}
