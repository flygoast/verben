#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "verben.h"
#include "anet.h"
#include "plugin.h"
#include "log.h"
#include "conf.h"
#include "threadpool.h"

#define HEADER_SIZE         10 /* protocol header size */
#define BACKEND_FF          1
#define BACKEND_SAVER       2
#define BACKEND_CACHE       3

#define SERVER_MAX          10
#define ARRAY_SIZE(a)       (sizeof(a)/sizeof(a[0]))

typedef struct server_config {
    char    *ip;
    int     port;
} server_config_t;

typedef struct task_item {
    int     backend;
    char    *data;
} task_item;


static server_config_t ff_list[SERVER_MAX];
static server_config_t cache_list[SERVER_MAX];
static server_config_t saver_list[SERVER_MAX];
static unsigned int ff_cnt;
static unsigned int cache_cnt;
static unsigned int saver_cnt;

static char *resp[] = {"ok\n", "er\n"};
static int conn_timeout;
static int send_timeout;
static int recv_timeout;
static threadpool_t *pool;
static int thread_init;
static int thread_max;
static int thread_stack;

#ifdef DEBUG
static long long mstime() {
    struct timeval tv = {};
    long long mst = 0;
    gettimeofday(&tv, NULL);
    mst = ((long long)tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    return mst;
}
#endif /* DEBUG */

static void load_server_config(conf_t *conf) {
    char *ip;
    int port;
    conf_block_t *block;
    block = conf_get_block(conf, "ff");

    while (block) {
        ip = conf_get_str_value(&block->block, "ip", NULL);
        if (!ip) continue;
        port = conf_get_int_value(&block->block, "port", 0);
        if (port == 0) continue;
        
        ff_list[ff_cnt].ip = strdup(ip);
        ff_list[ff_cnt].port = port;
        ++ff_cnt;
        block = block->next;
    }

    block = conf_get_block(conf, "saver");
    while (block) {
        ip = conf_get_str_value(&block->block, "ip", NULL);
        if (!ip) continue;
        port = conf_get_int_value(&block->block, "port", 0);
        if (port == 0) continue;
        
        saver_list[saver_cnt].ip = strdup(ip);
        saver_list[saver_cnt].port = port;
        ++saver_cnt;
        block = block->next;
    }

    block = conf_get_block(conf, "cache");
    while (block) {
        ip = conf_get_str_value(&block->block, "ip", NULL);
        if (!ip) continue;
        port = conf_get_int_value(&block->block, "port", 0);
        if (port == 0) continue;
        
        cache_list[cache_cnt].ip = strdup(ip);
        cache_list[cache_cnt].port = port;
        ++cache_cnt;
        block = block->next;
    }
}

void free_server_config() {
    server_config_t *sc;
    int i = 0; 
    for (i = 0; i < ff_cnt; ++i) {
        sc = &ff_list[i];
        if (sc->ip) {
            free(sc->ip);
            sc->ip = NULL;
        }
    }

    for (i = 0; i < saver_cnt; ++i) {
        sc = &saver_list[i];
        if (sc->ip) {
            free(sc->ip);
            sc->ip = NULL;
        }
    }

    for (i = 0; i < cache_cnt; ++i) {
        sc = &cache_list[i];
        if (sc->ip) {
            free(sc->ip);
            sc->ip = NULL;
        }
    }
}

static int tcp_connect_timeout(const char *ipaddr, unsigned short port, 
        int timeout) {
    int error;
    int len;
    int sockfd;
    unsigned long ul;
    int ret;
    fd_set w;
    struct sockaddr_in peer;
    struct timeval  tv;

    bzero(&peer, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    if (inet_pton(AF_INET, ipaddr, &peer.sin_addr) <= 0) {
        return -1;
    }

    if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    
    ul = 1;
    ret = ioctl(sockfd, FIONBIO, &ul);
    if (ret == -1) {
        close(sockfd);
        ERROR_LOG("ioctl set nonblocking failed");
        return -1;
    }
    
    if (connect(sockfd, (const struct sockaddr *)&peer, 
            sizeof(peer)) == 0) {
        return sockfd;
    }

    if (errno != EINPROGRESS) {
        ERROR_LOG("cannot connect to %s:%d", ipaddr, port);
        return -1;
    }
    
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    FD_ZERO(&w);
    FD_SET(sockfd, &w);
    ret = select(sockfd + 1, NULL, &w, NULL, &tv);

    if (ret == -1) {
        close(sockfd);
        ERROR_LOG("select failed:%s", strerror(errno));
        return -1;
    } else if (ret == 0) {
        close(sockfd);
        ERROR_LOG("select timeout");
        return -1;
    }
    if (FD_ISSET(sockfd, &w)) {
        len = sizeof(error);
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, 
                &error, (socklen_t*)&len) < 0) {
            return -1;
        }
    } else {
        ERROR_LOG("select error:sockfd not set");
        return -1;
    }

    ul = 0;
    ret = ioctl(sockfd, FIONBIO, &ul);
    if (ret == -1) {
        close(sockfd);
        ERROR_LOG("ioctl set blocking failed:%s", strerror(errno));
        return -1;
    }

    return sockfd;
}

static int adjust_timeout(struct timeval *tv, struct timeval *start) {
    struct timeval temp;
    long long ustart, unow, timeout;
    ustart = start->tv_sec * 1000000 + start->tv_usec;
    gettimeofday(&temp, NULL);
    unow = temp.tv_sec * 1000000 + temp.tv_usec;
    unow -= ustart;
    timeout = tv->tv_sec * 1000000 + tv->tv_usec;

    if (unow >= timeout) {
        return -1;
    }

    tv->tv_sec = (timeout - unow) / 1000000;
    tv->tv_usec = (timeout - unow) % 1000000;
    return 0;
}

static int tcp_send_timeout(int sockfd, const char *buf, int total, 
        int timeout) {
    struct timeval tv;
    struct timeval start;
    int send_bytes, cur_len;

    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    gettimeofday(&start, NULL);

    for (send_bytes = 0; send_bytes < total; 
            send_bytes += cur_len) {
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, 
                &tv, sizeof(tv)) < 0) {
            ERROR_LOG("setsockopt:set send timeout failed:%s",
                strerror(errno));
            return -1;

        }
        cur_len = send(sockfd, buf + send_bytes, total - send_bytes, 0);
        if (cur_len < 0) {
            if (errno == EAGAIN) {
                ERROR_LOG("send timeout");
                return -1;
            }
            ERROR_LOG("send failed:%s", strerror(errno));
            return -1;
        }
        if (adjust_timeout(&tv, &start) < 0 && 
                send_bytes + cur_len < total) {
            ERROR_LOG("tcp_send_timeout timeout");
            return -1;
        }
    }
    return send_bytes;
}

static int select_send(int *pfd, int *pcur, int type, char *buf, int length) {
    server_config_t *sc = NULL;
    int i = 0;
    int n = 0;

    if (*pfd >= 0 && 
            (n = tcp_send_timeout(*pfd, buf, 
                length, send_timeout)) == length) {
        return n;
    }

    if (*pfd >= 0) {
        close(*pfd);
    }
    *pfd = -1;

    switch (type) {
    case BACKEND_FF:
        for (i = 0; i < ff_cnt; ++i) {
            sc = &ff_list[*pcur % ff_cnt];
            *pcur = (*pcur + 1) % ff_cnt;
            *pfd = tcp_connect_timeout(sc->ip, sc->port, conn_timeout);
            if (*pfd < 0) {
                *pfd = -1;
                continue;
            }
    
            if ((n = tcp_send_timeout(*pfd, buf, length, send_timeout))
                    != length) {
                close(*pfd);
                continue;
            }
            return n;
        }
        break;
    case BACKEND_SAVER:
        for (i = 0; i < cache_cnt; ++i) {
            sc = &cache_list[*pcur % ff_cnt];
            *pcur = (*pcur + 1) % ff_cnt;
            *pfd = tcp_connect_timeout(sc->ip, sc->port, conn_timeout);
            if (*pfd < 0) {
                *pfd = -1;
                continue;
            }
    
            if ((n = tcp_send_timeout(*pfd, buf, length, send_timeout))
                    != length) {
                close(*pfd);
                continue;
            }
            return n;
        }
        break;
    case BACKEND_CACHE:
        for (i = 0; i < saver_cnt; ++i) {
            sc = &saver_list[*pcur % ff_cnt];
            *pcur = (*pcur + 1) % ff_cnt;
            *pfd = tcp_connect_timeout(sc->ip, sc->port, conn_timeout);
            if (*pfd < 0) {
                *pfd = -1;
                continue;
            }
    
            if ((n = tcp_send_timeout(*pfd, buf, length, send_timeout))
                    != length) {
                close(*pfd);
                continue;
            }
            return n;
        }
        break;
    }
    return -1;
}

static void transfer_data(void *arg) {
    static __thread int ff_sock = -1;
    static __thread int ff_cur = 0;
    static __thread int saver_sock = -1;
    static __thread int saver_cur = 0;
    static __thread int cache_sock = -1;
    static __thread int cache_cur = 0;

    task_item *item = (task_item *)arg;
    switch (item->backend) {
    case BACKEND_FF:
        if (select_send(&ff_sock, &ff_cur, BACKEND_FF,
                item->data, strlen(item->data)) < 0) {
            ERROR_LOG("Send to all ff failed");
            goto end;
        }
        break;
    case BACKEND_SAVER:
        if (select_send(&saver_sock, &saver_cur, BACKEND_SAVER,
                item->data, strlen(item->data)) < 0) {
            ERROR_LOG("Send to all saver failed");
            goto end;
        }
        break;
    case BACKEND_CACHE:
        if (select_send(&cache_sock, &cache_cur, BACKEND_CACHE,
                item->data, strlen(item->data)) < 0) {
            ERROR_LOG("Send to all cache failed");
            goto end;
        }
        break;
    }
end:
    if (item->data) free(item->data);
    free(arg);
}

int handle_init(void *cycle, int proc_type) {
    conf_t *conf = (conf_t*)cycle;

    switch (proc_type) {
        case VB_PROCESS_MASTER:
            return 0;
        case VB_PROCESS_WORKER:
            conn_timeout = conf_get_int_value(conf, "conn_timeout", 3);
            send_timeout = conf_get_int_value(conf, "send_timeout", 3);
            recv_timeout = conf_get_int_value(conf, "recv_timeout", 3);

            thread_init = conf_get_int_value(conf, "thread_init", 5);
            thread_max = conf_get_int_value(conf, "thread_max", 5);
            thread_stack = conf_get_int_value(conf, "thread_stack", 1048576);

            pool = threadpool_create(thread_init, thread_max, thread_stack);
            if (!pool) {
                boot_notify(-1, "worker[%lu]:%s", getpid(), "Create thread pool");
                return -1;
            }
            load_server_config(conf);

            return 0;
        case VB_PROCESS_CONN:
            return 0;
    }

    /* Never get here. */
    return -1;
}

/*
int handle_open(char **sendbuf, int *len, 
        const char *remote_ip, int port) {
    DEBUG_LOG("Receive a connection from %s:%d", remote_ip, port);
    return 0;
}
*/


/*
void handle_close(const char *remote_ip, int port) {
    DEBUG_LOG("connection from %s:%d closed", remote_ip, port);
}
*/

int handle_input(char *buf, int len, const char *remote_ip, int port) {
    int i;
    int protlen = 0;
    if (len < HEADER_SIZE) {
        return 0;
    } else {
        /* filter invalid input, such as HTTP request "GET / HTTP/1.1" */
        for (i = 0; i < HEADER_SIZE; ++i) {
            if (!isdigit(buf[i])) {
                return -1;
            }
            protlen = protlen * 10 + buf[i] - '0';
        }

        if (protlen == 0) {
            return -1;
        }
        return protlen + HEADER_SIZE;
    }
}

int handle_process(char *rcvbuf, int rcvlen, 
        char **sndbuf, int *sndlen, const char *remote_ip, int port) {
    task_item *item;
    char *ptr;
#ifdef DEBUG
    char buf[4096];
    memcpy(buf, rcvbuf, rcvlen);
    buf[rcvlen] = '\0';

    DEBUG_LOG("%lld: Receive packet from %s:%d, length:%d, Content:%s",
            mstime(), remote_ip, port, rcvlen, buf);
#endif /* DEBUG */

    ptr = malloc(3);
    if (!ptr) {
        *sndbuf = NULL;
        *sndlen = 0;
        return -1;
    }
    item = (task_item*)malloc(sizeof(*item));
    item->backend = BACKEND_FF;
    item->data = (char *)calloc(rcvlen + 1, 1);
    memcpy(item->data, rcvbuf, rcvlen);
    if (threadpool_add_task(pool, transfer_data, item, 0) != 0) {
        memcpy(ptr, resp[1], 3);
        *sndlen = 3;
        ERROR_LOG("Add task to threadpool failed:ff");
        return -1;
    }

    item = (task_item*)malloc(sizeof(*item));
    item->backend = BACKEND_SAVER;
    item->data = (char *)calloc(rcvlen + 1, 1);
    memcpy(item->data, rcvbuf, rcvlen);
    if (threadpool_add_task(pool, transfer_data, item, 0) != 0) {
        memcpy(ptr, resp[1], 3);
        *sndlen = 3;
        ERROR_LOG("Add task to threadpool failed:saver");
        return -1;
    }

    item = (task_item*)malloc(sizeof(*item));
    item->backend = BACKEND_CACHE;
    item->data = (char *)calloc(rcvlen + 1, 1);
    memcpy(item->data, rcvbuf, rcvlen);
    if (threadpool_add_task(pool, transfer_data, item, 0) != 0) {
        memcpy(ptr, resp[1], 3);
        *sndlen = 3;
        ERROR_LOG("Add task to threadpool failed:cache");
        return -1;
    }

    memcpy(ptr, resp[0], 3);
    *sndlen = 3;
    *sndbuf = ptr;
    return 0;
}

void handle_fini(void *cycle, int proc_type) {
    switch (proc_type) {
        case VB_PROCESS_MASTER:
            break;
        case VB_PROCESS_WORKER:
            for ( ; ; ) {
                if (g_pool->threads_idle == g_pool->threads_num) {
                    break;
                }
                sleep(1);
            }

            threadpool_destroy(pool);
            free_server_config();
            break;
        case VB_PROCESS_CONN:
            break;
    }
}
