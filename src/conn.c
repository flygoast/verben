#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include "verben.h"
#include "conn.h"
#include "dlist.h" 
#include "sds.h"
#include "anet.h"
#include "shmq.h"
#include "ae.h"
#include "log.h"
#include "conf.h"
#include "notifier.h"

#define IOBUF_SIZE      4096
#define MAX_PROT_LEN    4096

ae_event_loop   *ael;
static int      listen_fd;
static char     sock_error[ANET_ERR_LEN];
static dlist    *clients; 
static int      client_limit;
static int      client_timeout;
static time_t   unix_clock;

static void free_client_node(void *cli) {
    client_conn *c = (client_conn *)cli;
    if (c->remote_ip) free(c->remote_ip);
    sdsfree(c->recvbuf);
    sdsfree(c->sendbuf);
    if (c) free(c);
}

static void free_client(client_conn *cli) {
    dlist_node *ln;
    ae_delete_file_event(ael, cli->fd, AE_READABLE);
    ae_delete_file_event(ael, cli->fd, AE_WRITABLE);

    if (dll.handle_close) {
        dll.handle_close(cli->remote_ip, cli->remote_port);
    }

    close(cli->fd);
    ln = dlist_search_key(clients, cli);
    /* This line of code will free the 'cli' node automately. */
    dlist_delete_node(clients, ln);
}

static int reduce_client_refcount(client_conn *cli) {
    if (--cli->refcount < 0) {
        free_client(cli);
        return -1;
    }
    return 0;
}

static int server_cron(ae_event_loop *el, long long id, void *privdate) {
    dlist_iter iter;
    dlist_node *node;
    client_conn *cli;
    unix_clock = time(NULL);
    dlist_rewind(clients, &iter);

    while ((node = dlist_next(&iter))) {
        cli = node->value;
        if (cli->refcount == 0 && client_timeout &&
                unix_clock - cli->access_time > client_timeout) {
            reduce_client_refcount(cli);
        }
    }

    return 1000;
}

static void read_from_client(ae_event_loop *el, int fd, 
        void *privdata, int mask) {
    client_conn *cli = (client_conn *)privdata;
    int nread;
    char buf[IOBUF_SIZE];
    AE_NOTUSED(el);
    AE_NOTUSED(mask);
    
    nread = read(fd, buf, IOBUF_SIZE);
    cli->access_time = unix_clock;
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
            return;
        } else {
            ERROR_LOG("read failed from %d:%s", cli->fd, strerror(errno));
            reduce_client_refcount(cli);
            return;
        }
    } else if (nread == 0) {
        NOTICE_LOG("Client close fd %d", cli->fd);
        reduce_client_refcount(cli);
        return;
    }
    buf[nread] = '\0';

    cli->recvbuf = sdscatlen(cli->recvbuf, buf, nread);

    /* The plugin should definite the `handle_input` to process
       the network protocol. */
    if (cli->recv_prot_len == 0) { /* unknown protocol length */
        cli->recv_prot_len = dll.handle_input(cli->recvbuf, 
                sdslen(cli->recvbuf), cli->remote_ip, cli->remote_port);
    }

    if (cli->recv_prot_len < 0 || cli->recv_prot_len > MAX_PROT_LEN) {
        /* invalid protocol length */
        ERROR_LOG("Invalid protocol length:%d", cli->recv_prot_len);
        reduce_client_refcount(cli);
    } else if (cli->recv_prot_len == 0) {
        /* unknown protocol length */
        /* process big protocol */
        /* Do nothing, just continue to receive data. */
    } else if (sdslen(cli->recvbuf) >= cli->recv_prot_len) {
        /* integrity protocol. We'll put the entire datagram into
         * shared memory queue to feed the worker processes. */
        shm_msg *msg = (shm_msg*)malloc(sizeof(*msg) + cli->recv_prot_len);
        if (!msg) {
            ERROR_LOG("Create message failed for fd %d", cli->fd);
            reduce_client_refcount(cli);
            return;
        }
        msg->cli = cli;
        strncpy(msg->remote_ip, cli->remote_ip, 16);
        msg->remote_port = cli->remote_port;
        memcpy(msg->data, cli->recvbuf, cli->recv_prot_len);

        if (shmq_push(recv_queue, msg, 
                    sizeof(*msg) + cli->recv_prot_len, 0) != 0) {
            ERROR_LOG("shmq push failed for fd:%d", cli->fd);
            reduce_client_refcount(cli);
        }
        ++cli->refcount;
        free(msg);
        cli->recvbuf = sdsrange(cli->recvbuf, cli->recv_prot_len, -1);
        cli->recv_prot_len = 0;
    }
}

static client_conn *create_client(int cli_fd, char *cli_ip, int cli_port) {
    client_conn *cli = malloc(sizeof(*cli));
    if (!cli) {
        ERROR_LOG("create client connection structure failed");
        return NULL;
    }

    anet_nonblock(sock_error, cli_fd);
    anet_tcp_nodelay(sock_error, cli_fd);

    if (ae_create_file_event(ael, cli_fd, AE_READABLE,
            read_from_client, cli) == AE_ERR) {
        ERROR_LOG("Create read file event failed");
        close(cli_fd);
        free(cli);
        return NULL;
    }

    cli->fd = cli_fd;
    cli->refcount = 0;
    cli->recv_prot_len = 0;
    cli->remote_ip = strdup(cli_ip);
    cli->remote_port = cli_port;
    cli->recvbuf = sdsempty();
    cli->sendbuf = sdsempty();
    cli->access_time = unix_clock;
    if (!dlist_add_node_tail(clients, cli)) {
        ERROR_LOG("Add client node to linked list failed");
        close(cli_fd);
        free(cli);
        return NULL;
    }

    return cli;
}

static void write_to_client(ae_event_loop *el, int fd, 
        void *privdata, int mask) {
    client_conn *cli = (client_conn*)privdata;
    int nwrite;
    AE_NOTUSED(el);
    AE_NOTUSED(mask);

    nwrite = write(fd, cli->sendbuf, sdslen(cli->sendbuf));
    cli->access_time = unix_clock;
    if (nwrite < 0) {
        if (errno == EAGAIN) {
            nwrite = 0;
        } else {
            reduce_client_refcount(cli);
            return;
        }
    }

    if (nwrite == sdslen(cli->sendbuf)) {
        ae_delete_file_event(el, cli->fd, AE_WRITABLE);
        sdsclear(cli->sendbuf);
    } else {
        /* process the left buffer */
        cli->sendbuf = sdsrange(cli->sendbuf, nwrite, -1);
    }
}

static void accept_common_handler(int cli_fd, char *cli_ip, int cli_port) {
    char *retbuf = NULL;
    int len;
    client_conn *c = create_client(cli_fd, cli_ip, cli_port);
    if (!c) {
        ERROR_LOG("Allocating resources for client failed");
        close(cli_fd); /* May be already closed, just ignore errors. */
        return;
    }

    if (client_limit && dlist_length(clients) > client_limit) {
        ERROR_LOG("Max number of clients reached");
        reduce_client_refcount(c);
        return;
    }

    if (dll.handle_open) {
        if (dll.handle_open(&retbuf, &len, cli_ip, cli_port) != 0) {
            WARNING_LOG("Close socket %d according to handle_open hook",
                    c->fd);
            reduce_client_refcount(c);
            return;
        } else {
            /* You can send something such as welcome information once
             * upon client's connection. */
            if (retbuf != NULL) {
                c->sendbuf = sdscatlen(c->sendbuf, retbuf, len);
                if (ae_create_file_event(ael, c->fd, AE_WRITABLE, 
                            write_to_client, c) == AE_ERR) {
                    ERROR_LOG("Create write file event failed on fd %d",
                        c->fd);
                    reduce_client_refcount(c);
                }
            }
        }
    }
}

static void accept_handler(ae_event_loop *el, int fd, 
        void *privdata, int mask) {
    int cli_fd, cli_port;
    char cli_ip[16];
    AE_NOTUSED(el);
    AE_NOTUSED(mask);
    AE_NOTUSED(privdata);

    cli_fd = anet_tcp_accept(sock_error, fd, cli_ip, &cli_port);
    if (cli_fd == ANET_ERR) {
        ERROR_LOG("Accept failed:%s", sock_error);
        return;
    }

    DEBUG_LOG("Receive connection from %s:%d", cli_ip, cli_port);
    accept_common_handler(cli_fd, cli_ip, cli_port);
}

static void notifier_handler(ae_event_loop *el, int fd,
        void *privdata, int mask) {
    shm_msg *msg;
    int len;
    client_conn *cli;

    AE_NOTUSED(el);
    AE_NOTUSED(mask);
    AE_NOTUSED(privdata);
   
    if (notifier_read() <= 0) {
        ERROR_LOG("notifier_read failed:%s", strerror(errno));
        return;
    }

    /* Retrive all processed protocol datagram. */
    while (shmq_pop(send_queue, (void**)&msg, &len, 0) == 0) {
        cli = msg->cli;
        if (reduce_client_refcount(cli) >= 0) {
            cli->sendbuf = sdscatlen(cli->sendbuf, msg->data, 
                    len - sizeof(shm_msg));
            if (ae_create_file_event(el, cli->fd, AE_WRITABLE, 
                    write_to_client, cli) == AE_ERR) {
                ERROR_LOG("Create write file event failed on fd %d",
                        cli->fd);
                reduce_client_refcount(cli);
            }
        }
        free(msg);
    }
}

void conn_process_cycle(void *data) {
    char *host;
    int port;
    conf_t *conf = (conf_t*)data;
    int notifier = notifier_read_fd();
    vb_process = VB_PROCESS_CONN;

    client_limit = conf_get_int_value(conf, "client_limit", 0);
    client_timeout = conf_get_int_value(conf, "client_timeout", 60);

    /* Initialize client connection linked list. */
    clients = dlist_init(); 
    dlist_set_free(clients, free_client_node);

    if (!clients) {
        FATAL_LOG("Initialize clients connection linked list failed");
        exit(0);
    }

    ael = ae_create_event_loop();
    if (!ael) {
        FATAL_LOG("Initalize event loop structure failed");
        exit(0);
    }

    host = conf_get_str_value(conf, "server", "0.0.0.0");
    port = conf_get_int_value(conf, "port", 8773);
    listen_fd = anet_tcp_server(sock_error, host, port);
    if (listen_fd == ANET_ERR) {
        FATAL_LOG("Listen socket[%s:%d] failed:%s",
                host, port, sock_error);
        exit(0);
    }

    if (ae_create_time_event(ael, 1, server_cron, NULL, NULL) == AE_ERR) {
        FATAL_LOG("Create time event failed");
        exit(0);
    }

    if (ae_create_file_event(ael, notifier, AE_READABLE, 
                notifier_handler, NULL) == AE_ERR) {
        FATAL_LOG("Create notifier file event failed");
        exit(0);
    }

    if (listen_fd > 0 && ae_create_file_event(ael, listen_fd, 
                AE_READABLE, accept_handler, NULL) == AE_ERR) {
        FATAL_LOG("Create accept file event failed");
        exit(0);
    }

    ae_main(ael);
    if (dll.handle_fini) {
        dll.handle_fini(data, vb_process);
    }

    ae_free_event_loop(ael);
    dlist_destroy(clients);
    DEBUG_LOG("Conn process[%d] will exit", getpid());
    exit(0);
}
