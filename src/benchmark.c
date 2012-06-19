#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#include "ae.h"
#include "dlist.h"
#include "sds.h"
#include "anet.h"

static struct config {
    ae_event_loop   *el;
    char            *hostip;
    int             hostport;
    int             num_clients;
    int             live_clients;
    int             requests;
    int             requests_issued;
    int             requests_finished;
    int             keysize;
    int             datasize;
    int             quiet;
    long long       start;
    long long       total_latency;
    long long       *latency;
    char            *title;
    dlist           *clients;
} conf;

typedef struct client_st {
    int             fd;
    sds             obuf;
    unsigned int    written;    /* bytes of 'obuf' already written */
    unsigned int    read;       /* bytes already be read */
    long long       start;      /* start time of request */
    long long       latency;    /* request latency */
} client;


static void client_done(client *c);

static long long ustime() {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long)tv.tv_sec) * 1000000;
    ust += tv.tv_usec;
    return ust;
}

static long long mstime() {
    struct timeval tv;
    long long mst;

    gettimeofday(&tv, NULL);
    mst = ((long)tv.tv_sec) * 1000;
    mst += tv.tv_usec / 1000;
    return mst;
}

static void free_client(client *c) {
    dlist_node *node;
    ae_delete_file_event(conf.el, c->fd, AE_WRITABLE);
    ae_delete_file_event(conf.el, c->fd, AE_READABLE);
    close(c->fd);
    sdsfree(c->obuf);
    --conf.live_clients;
    node = dlist_search_key(conf.clients, c);
    assert(node != NULL);
    dlist_delete_node(conf.clients, node);
    free(c);
}

static void read_handler(ae_event_loop *el, int fd, void *priv, int mask) {
    client *c = (client *)priv;
    int nread;
    char buffer[4096];

    nread = read(fd, buffer, 4096);
    if (nread == -1) {
        if (errno == EAGAIN) {
            return;
        }
        fprintf(stderr, "Error: %s\n", strerror(errno));
        exit(1);
    } else if (nread == 0) {
        fprintf(stderr, "Error: %s\n", "Server close connection.");
        exit(1);
    }
    
    c->read += nread;
    if (c->read == sdslen(c->obuf)) {
        c->latency = ustime() - c->start;
        client_done(c);
    }
}

static void write_handler(ae_event_loop *el, int fd, void *priv, int mask) {
    client *c = (client *)priv;

    /* Initialize request when nothing was written. */
    if (c->written == 0) {
        if (conf.requests_issued++ >= conf.requests) {
            free_client(c);
            return;
        }

        c->start = ustime();
        c->latency = -1;
    }

    if (sdslen(c->obuf) > c->written) {
        char *ptr = c->obuf + c->written;
        int nwritten = write(c->fd, ptr, sdslen(c->obuf) - c->written);
        if (nwritten == -1) {
            if (errno != EPIPE) {
                fprintf(stderr, "write failed:%s\n", strerror(errno));
            }
            free_client(c);
            return;
        }
        c->written += nwritten;

        if (sdslen(c->obuf) == c->written) {
            ae_delete_file_event(conf.el, c->fd, AE_WRITABLE);
            ae_create_file_event(conf.el, c->fd, 
                    AE_READABLE, read_handler, c);
        }
    }

}

static client *create_client(const char *content, int len) {
    client *c = (client *)malloc(sizeof(*c));
    c->fd = anet_tcp_nonblock_connect(NULL, conf.hostip, conf.hostport);
    if (c->fd == ANET_ERR) {
        fprintf(stderr, "Connect to %s:%d failed\n", 
                conf.hostip, conf.hostport);
        exit(1);
    }
    c->obuf = sdsnewlen(content, len);
    c->written = 0;
    c->read = 0;
    ae_create_file_event(conf.el, c->fd, AE_WRITABLE, write_handler, c);
    dlist_add_node_tail(conf.clients, c);
    ++conf.live_clients;
    return c;
}

static void create_missing_clients(client *c) {
    int n = 0;
    while (conf.live_clients < conf.num_clients) {
        create_client(c->obuf, sdslen(c->obuf));
        if (++n > 64) {
            usleep(50000);
            n = 0;
        }
    }
}

static void client_done(client *c) {
    if (conf.requests_finished == conf.requests) {
        free_client(c);
        ae_stop(conf.el);
        return;
    }

    --conf.live_clients;
    create_missing_clients(c);
    ++conf.live_clients;
    free_client(c);
}

static void free_all_clients() {
    dlist_node *node = conf.clients->head, *next;
    while (node) {
        next = node->next;
        free_client((client *)node->value);
        node = next;
    }
}

static void show_latency_report(void) {
    float   perc, reqpersec;

    reqpersec = (float)conf.requests_finished 
        / ((float)conf.total_latency / 1000);

    if (!conf.quiet) {
        printf("========== %s ==========\n", conf.title);
        printf(" %d requests completed in %.2f seconds\n",
                conf.requests_finished, (float)conf.total_latency/1000);
        printf(" %d parallel clients\n", conf.num_clients);
        printf("\n");
        printf("%.2f requests per second\n\n", reqpersec);
    } else {
        printf("%s:%.2f requests per second\n\n", conf.title, reqpersec);
    }
}

static void benchmark(char *title, char *content, int len) {
    client  *c;
    conf.title = title;
    conf.requests_issued = 0;
    conf.requests_finished = 0;

    c = create_client(content, len);
    create_missing_clients(c);

    conf.start = mstime();
    ae_main(conf.el);
    conf.total_latency = mstime() - conf.start;

    show_latency_report();
    free_all_clients();
}

static int show_throughput(ae_event_loop *el, long long id, void *priv) {
    float dt = (float)(mstime() - conf.start) / 1000.0;
    float rps = (float)conf.requests_finished / dt;
    printf("%s: %.2f\n", conf.title, rps);
    return 250; /* every 250ms */
}


int main(int argc, char **argv) {
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    conf.num_clients = 50;
    conf.requests = 10000;
    conf.live_clients = 0;
    conf.el = ae_create_event_loop();
    ae_create_time_event(conf.el, 1, show_throughput, NULL, NULL);
    conf.latency = NULL;
    conf.clients = dlist_init();
    conf.hostip = "127.0.0.1";
    conf.hostport = 8773;
    
    benchmark("QPS benchmark", "hello\r\n", 7);
    exit(0);
}
