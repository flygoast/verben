/* A simple event-driven programming library. It's from Redis. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include "ae.h"

/* Include the best multiplexing layer supported by this system.
   The following should be ordered by performances, descending. */
#if defined(EPOLL_MODE)
#include "ae_epoll.c"
#elif defined(POLL_MODE)
#include "ae_poll.c"
#elif defined(KQUEUE_MODE)
#include "ae_kqueue.c"
#else
#include "ae_select.c"
#endif

/*
#if defined(__linux__) 
#include "ae_epoll.c"
#elif defined(__FreeBSD__)
#include "ae_kqueue.c"
#else
#include "ae_select.c"
#endif
*/

ae_event_loop *ae_create_event_loop(void) {
    ae_event_loop *el;
    int i;

    el = (ae_event_loop*)malloc(sizeof(*el));
    if (!el) {
        return NULL;
    }
    el->time_event_head = NULL;
    el->time_event_next_id = 0;
    el->stop = 0;
    el->maxfd = -1;
    el->before_sleep = NULL;
    if (ae_api_create(el) == -1) {
        free(el);
        return NULL;
    }

    /* Events with mask == AE_NONE are not set. So let's initialize
       the vector with it. */
    for (i = 0; i < AE_SETSIZE; ++i) {
        el->events[i].mask = AE_NONE;
    }
    return el;
}

void ae_free_event_loop(ae_event_loop *el) {
    ae_api_free(el);
    free(el);
}

void ae_stop(ae_event_loop *el) {
    el->stop = 1;
}

/* Register a file event. */
int ae_create_file_event(ae_event_loop *el, int fd, int mask,
        ae_file_proc *proc, void *client_data) {
    if (fd >= AE_SETSIZE) {
        return AE_ERR;
    } 
    ae_file_event *fe = &el->events[fd];

    if (ae_api_add_event(el, fd, mask) == -1) {
        return AE_ERR;
    }
    fe->mask |= mask; /* On one fd, multi events can be registered. */
    if (mask & AE_READABLE) {
        fe->r_file_proc = proc;
    }

    if (mask & AE_WRITABLE) {
        fe->w_file_proc = proc;
    } 
    fe->client_data = client_data;
    /* Once one file event has been registered, the el->maxfd
       no longer is -1. */
    if (fd > el->maxfd) {
        el->maxfd = fd;
    }
    return AE_OK;
}

/* Unregister a file event */
void ae_delete_file_event(ae_event_loop *el, int fd, int mask) {
    if (fd >= AE_SETSIZE) {
        return;
    }

    ae_file_event *fe = &el->events[fd];
    if (fe->mask == AE_NONE) {
        return;
    }
    fe->mask = fe->mask & (~mask);
    if (fd == el->maxfd && fe->mask == AE_NONE) {
        /* All the events on the fd were deleted, update the max fd. */
        int j;
        for (j = el->maxfd - 1; j >= 0; --j) {
            if (el->events[j].mask != AE_NONE) {
                break;
            }
        }
        /* If all the file events on all fds deleted, max fd will get
           back to -1. */
        el->maxfd = j;
    }
    ae_api_del_event(el, fd, mask);
}

int ae_get_file_events(ae_event_loop *el, int fd) {
    if (fd >= AE_SETSIZE) {
        return 0;
    }

    ae_file_event *fe = &el->events[fd];
    return fe->mask;
}

static void ae_get_time(long *seconds, long *milliseconds) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec / 1000;
}

static void ae_add_milliseconds_to_now(long long milliseconds, 
        long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;
    ae_get_time(&cur_sec, &cur_ms);
    when_sec = cur_sec + milliseconds / 1000;
    when_ms = cur_ms + milliseconds % 1000;
    /* cur_ms < 1000, when_ms < 2000, so just one time is enough. */
    if (when_ms >= 1000) {
        ++when_sec;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

/* Register a time event. */
long long ae_create_time_event(ae_event_loop *el, long long ms, 
        ae_time_proc *proc, void *client_data,
        ae_event_finalizer_proc *finalizer_proc) {
    long long id = el->time_event_next_id++;
    ae_time_event *te;
    te = (ae_time_event*)malloc(sizeof(*te));
    if (te == NULL) {
        return AE_ERR;
    }
    te->id = id;
    ae_add_milliseconds_to_now(ms, &te->when_sec, &te->when_ms);
    te->time_proc = proc;
    te->finalizer_proc = finalizer_proc;
    te->client_data = client_data;
    /* Insert time event into the head of the linked list. */
    te->next = el->time_event_head;
    el->time_event_head = te;
    return id;
}

/* Unregister a time event. */
int ae_delete_time_event(ae_event_loop *el, long long id) {
    ae_time_event *te, *prev = NULL;

    te = el->time_event_head;
    while (te) {
        if (te->id == id) {
            /* Delete a node from the time events linked list. */
            if (prev == NULL) {
                el->time_event_head = te->next;
            } else {
                prev->next = te->next;
            }
            if (te->finalizer_proc) {
                te->finalizer_proc(el, te->client_data);
            }
            free(te);
            return AE_OK;
        }
        prev = te;
        te = te->next;
    }
    return AE_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
   This operation is useful to know how many time the select can be
   put in sleep without to delay any event.
   If there are no timers NULL is returned.

   Note that's O(N) since time events are unsorted.
   Possible optimizations (not needed by Redis so far, but ...):
   1) Insert the event in order, so that the nearest is just the head.
      Much better but still insertion or deletion of timer is O(N).
   2) Use a skiplist to have this operation as O(1) and insertion as
      O(log(N)).

    At now, I don't know what's a skiplist...... 0_0!
*/
static ae_time_event *ae_search_nearest_timer(ae_event_loop *el) {
    ae_time_event *te = el->time_event_head;
    ae_time_event *nearest = NULL;

    while (te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
                (te->when_sec == nearest->when_sec &&
                 te->when_ms < nearest->when_ms)) {
            nearest = te;
        }
        te = te->next;
    }
    return nearest;
}

/* Process time events. */
static int process_time_events(ae_event_loop *el) {
    int processed = 0;
    ae_time_event *te;
    long long maxid;

    te = el->time_event_head;
    maxid = el->time_event_next_id - 1;
    while (te) {
        long now_sec, now_ms;
        long long id;
        /* Don't process the time event registered during this process. */
        if (te->id > maxid) { 
            te = te->next;
            continue;
        }
        ae_get_time(&now_sec, &now_ms);
        /* timeout */
        if (now_sec > te->when_sec ||
            (now_sec == te->when_sec && now_ms >= te->when_ms)) {
            int ret;
            id = te->id;
            ret = te->time_proc(el, id, te->client_data);
            processed++;
            /* After an event is processed our time event list 
               may no longer be the same, so we restart from head.
               Still we make sure to don't process events registered
               by event handlers itself in order to don't loop
               forever. To do so we saved the max ID we want to 
               handle.

               FUTURE OPTIMIZATIONS:
               Note that this is NOT great algorithmically. Redis
               uses a single time event so it's not a problem but
               the right way to do this is to add the new elements
               on head, and to flag deleted elements in a special
               way for later deletion(putting references to the 
               nodes to delete into another linked list). */
            if (ret > 0) {
                ae_add_milliseconds_to_now(ret, &te->when_sec, 
                        &te->when_ms);
            } else {
                ae_delete_time_event(el, id);
            }
            te = el->time_event_head;
        } else {
            te = te->next;
        }
    }
    return processed;
}

/* Process every pending time event, then every pending file event
   (that may be registered by time event callbacks just processed).
   Without special flags the function sleeps until some file event
   fires, or when the next time event occurs (if any).

   If flag is 0, the function does nothing and returns.
   if flag has AE_ALL_EVENTS set, all the kind of events are processed.
   if flag has AE_FILE_EVENTS set, file events are processed.
   if flag has AE_TIME_EVENTS set, time events are processed.
   if flag has AE_DONT_WAIT set, the function returns ASAP (As soon
   as possible) until all the events that's possible to process 
   without to wait are processed.

   The function returns the number of events processed. */
int ae_process_events(ae_event_loop *el, int flags) {
    int processed = 0, numevents;

    /* Nothing to do ? return ASAP */
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) {
        return 0;
    }

    /* Note that we want call select() even if there are no file
       events to process as long as we want to process time events,
       in order to sleep until the next time event is ready to fire. */
    if (el->maxfd != -1 ||
        ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        int j;
        ae_time_event *shortest = NULL;
        struct timeval tv, *tvp;
        
        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT)) {
            shortest = ae_search_nearest_timer(el);
        } 
        if (shortest) {
            long now_sec, now_ms;

            /* Calculate the time missing for the nearest timer 
               to fire. */
            ae_get_time(&now_sec, &now_ms);
            tvp = &tv;
            tvp->tv_sec = shortest->when_sec - now_sec;
            if (shortest->when_ms < now_ms) {
                tvp->tv_usec = ((shortest->when_ms + 1000) 
                        - now_ms) * 1000;
                --tvp->tv_sec;
            } else {
                tvp->tv_usec = (shortest->when_ms - now_ms) * 1000;
            }
            if (tvp->tv_sec < 0) {
                tvp->tv_sec = 0;
            } 
            if (tvp->tv_usec < 0) {
                tvp->tv_usec = 0;
            }
        } else {
            /* If we have to check for events but need to return
               ASAP because of AE_DONT_WAIT we need to set the 
               timeout to zero. */
            if (flags & AE_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block. */
                tvp = NULL; /* wait forever */
            }
        }
        numevents = ae_api_poll(el, tvp);
        for (j = 0; j < numevents; ++j) {
            ae_file_event *fe = &el->events[el->fired[j].fd];
            int mask = el->fired[j].mask;
            int fd = el->fired[j].fd;
            int rfired = 0;
            
            /* Note the fe->mask & mask & ... code: maybe an already
               processed event removed an element that fired and we
               still didn't processed, so we check if the events is 
               still valid. */
            if (fe->mask & mask & AE_READABLE) {
                rfired = 1;
                fe->r_file_proc(el, fd, fe->client_data, mask);
            } 
            if (fe->mask & mask & AE_WRITABLE) {
                if (!rfired || fe->w_file_proc != fe->r_file_proc) {
                    fe->w_file_proc(el, fd, fe->client_data, mask);
                }
            }

            ++processed;
        }
    }
    /* Check time events */
    if (flags & AE_TIME_EVENTS) {
        processed += process_time_events(el);
    }
    /* Return the number of processed file/time events */
    return processed; 
}

/* Wait for milliseconds until the given file descriptior becomes
   writable/readable/exception */
int ae_wait(int fd, int mask, long long milliseconds) {
    struct timeval tv;
    fd_set rfds, wfds, efds;
    int retmask = 0, retval;

    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);

    if (mask & AE_READABLE) {
        FD_SET(fd, &rfds);
    }
    if (mask & AE_WRITABLE) {
        FD_SET(fd, &wfds);
    }
    if ((retval = select(fd + 1, &rfds, &wfds, &efds, &tv)) > 0) {
        if (FD_ISSET(fd, &rfds)) {
            retmask |= AE_READABLE;
        }
        if (FD_ISSET(fd, &wfds)) {
            retmask |= AE_WRITABLE;
        }
        return retmask;
    } else {
        return retval;
    }
}

/* main loop of the event-driven framework */
void ae_main(ae_event_loop *el) {
    el->stop = 0;
    while (!el->stop) {
        if (el->before_sleep != NULL) {
            el->before_sleep(el);
        }
        ae_process_events(el, AE_ALL_EVENTS);
    }
}

char *ae_get_api_name(void) {
    return ae_api_name();
}

void ae_set_before_sleep_proc(ae_event_loop *el, 
        ae_before_sleep_proc *before_sleep) {
    el->before_sleep = before_sleep;
}

#ifdef AE_TEST_MAIN
int print_timeout(ae_event_loop *el, long long id, void *client_data) {
    printf("Hello, AE!\n");
    return 5000; /* return AE_NOMORE */
}

struct items {
    char        *item_name;
    int         freq_sec;
    long long   event_id;
} acq_items [] = {
    {"acq1", 10 * 1000, 0},
    {"acq2", 30 * 1000, 0},
    {"acq3", 60 * 1000, 0},
    {"acq4", 10 * 1000, 0},
    {NULL, 0, 0}
};

int output_result(ae_event_loop *el, long long id, void *client_data) {
    int i = 0;
    for ( ; ; ++i) {
        if (!acq_items[i].item_name) {
            break;
        }
        if (id == acq_items[i].event_id) {
            printf("%s:%d\n", acq_items[i].item_name, time(NULL));
            fflush(stdout);
            return acq_items[i].freq_sec;
        }
    }
    return 0;
}

void add_all(ae_event_loop *el) {
    int i = 0;
    for ( ; ; ++i) {
        if (!acq_items[i].item_name) {
            break;
        }
        acq_items[i].event_id = ae_create_time_event(el, 
            acq_items[i].freq_sec,
            output_result, NULL, NULL);
    }
}

void delete_all(ae_event_loop *el) {
    int i = 0;
    for ( ; ; ++i) {
        if (!acq_items[i].item_name) {
            break;
        }

        ae_delete_time_event(el, acq_items[i].event_id);
    }
}

int main(int argc, char *argv[]) {
    long long id;
    ae_event_loop *el = ae_create_event_loop();
    add_all(el);
    ae_main(el);
    delete_all(el);
    exit(0);
}
#endif /* AE_TEST_MAIN */
