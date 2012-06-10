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

#define IOBUF_SIZE      4096
#define MAX_PROT_LEN    4096

extern shmq_t *recv_queue;
extern shmq_t *send_queue;
ae_event_loop    *ael;
static int              listen_fd;
static char sock_error[ANET_ERR_LEN];
static dlist            *clients; 

static int server_cron(ae_event_loop *el, long long id, void *privdate) {
    return 1000;
}

static void free_client(client_conn *cli) {
    dlist_node *ln;
    ae_delete_file_event(ael, cli->fd, AE_READABLE);
    ae_delete_file_event(ael, cli->fd, AE_WRITABLE);

    close(cli->fd);
    ln = dlist_search_key(clients, cli);
    dlist_delete_node(clients, ln);
    free(cli);
}

static void read_from_client(ae_event_loop *el, int fd, 
        void *privdata, int mask) {
    client_conn *cli = (client_conn *)privdata;
    int nread;
    char buf[IOBUF_SIZE];
    AE_NOTUSED(el);
    AE_NOTUSED(mask);
    
    nread = read(fd, buf, IOBUF_SIZE);
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            free_client(cli);
            return;
        }
    } else if (nread == 0) {
        free_client(cli);
        return;
    }
    buf[nread] = '\0';

    cli->recvbuf = sdscatlen(cli->recvbuf, buf, nread);

#ifdef DEBUG
    printf("recv:%s\n", cli->recvbuf);
#endif /* DEBUG */
    
    /* The plugin should definite the `handle_input` to process
       the network protocol. */
    if (cli->recv_prot_len == 0) { /* unknown protocol length */
        cli->recv_prot_len = dll.handle_input(cli->recvbuf, 
                sdslen(cli->recvbuf), &cli->sk);
    }

    if (cli->recv_prot_len < 0 || cli->recv_prot_len > MAX_PROT_LEN) {
        /* invalid protocol length */
        free_client(cli);
    } else if (cli->recv_prot_len == 0) {
        /* unknown protocol length */
        /* process big protocol */
    } else if (sdslen(cli->recvbuf) >= cli->recv_prot_len) {
        /* integrity protocol. We'll put the entire datagram into
         * shared memory queue to feed the worker processes. */
        shm_msg *msg = (shm_msg*)malloc(sizeof(*msg) + cli->recv_prot_len);
        msg->cli = cli;
        printf("shmq_push:%p\n", msg->cli);
        memcpy(&msg->sk, &cli->sk, sizeof(sock_info));
        msg->accept_fd = cli->accept_fd;
        memcpy(msg->data, cli->recvbuf, cli->recv_prot_len);

        if (shmq_push(recv_queue, msg, 
                    sizeof(*msg) + cli->recv_prot_len, 0) != 0) {
            free_client(cli);
        }
        free(msg);
        assert(sdsrange(cli->recvbuf, cli->recv_prot_len, -1));
        cli->recv_prot_len = 0;
        printf("GET HERE\n");
    }
}

static int add_client_conn(int cli_fd) {
    client_conn *cli = malloc(sizeof(*cli));
    assert(cli);
    anet_nonblock(sock_error, cli_fd);
    anet_tcp_nodelay(sock_error, cli_fd);

    if (ae_create_file_event(ael, cli_fd, AE_READABLE,
            read_from_client, cli) == AE_ERR) {
        close(cli_fd);
        free(cli);
        return -1;
    }

    cli->fd = cli_fd;
    cli->recv_prot_len = 0;
    cli->recvbuf = sdsempty();
    cli->sendbuf = sdsempty();
    dlist_add_node_tail(clients, cli);

    printf("Add read event fd: %d\n", cli_fd);
    return 0;
}

static void write_to_client(ae_event_loop *el, int fd, 
        void *privdata, int mask) {
    client_conn *cli = (client_conn*)privdata;
    int nwrite;
    AE_NOTUSED(el);
    AE_NOTUSED(mask);

    nwrite = write(fd, cli->sendbuf, sdslen(cli->sendbuf));
#ifdef DEBUG
    printf("To send: %d, Actual send:%d, %s", nwrite, 
            sdslen(cli->sendbuf), cli->sendbuf);
#endif /* DEBUG */

    if (nwrite < 0) {
        if (errno == EAGAIN) {
            nwrite = 0;
        } else {
            free_client(cli);
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

static void accept_common_handler(int cli_fd) {
    /* TODO */
    /* Add filter and limit. */
    if (add_client_conn(cli_fd) < 0) {
        fprintf(stderr, "add_client failed\n");
        close(cli_fd);
    }
}

static void accept_handler(ae_event_loop *el, int fd, 
        void *privdata, int mask) {
    int cli_fd, cli_port;
    char cli_ip[128];
    AE_NOTUSED(el);
    AE_NOTUSED(mask);
    AE_NOTUSED(privdata);

    cli_fd = anet_tcp_accept(sock_error, fd, cli_ip, &cli_port);
    printf("PID:%d Accept fd:%d\n", getpid(), cli_fd);
    if (cli_fd == ANET_ERR) {
        fprintf(stderr, "%s\n", sock_error);
        return;
    }
    accept_common_handler(cli_fd);
}

static void notifier_handler(ae_event_loop *el, int fd,
        void *privdata, int mask) {
    shm_msg *msg;
    int len;
    client_conn *cli;
    char *buf;

    AE_NOTUSED(el);
    AE_NOTUSED(mask);
    AE_NOTUSED(privdata);
   
    notifier_read();
    while (shmq_pop(send_queue, (void**)&msg, &len, 0) == 0) {
        cli = msg->cli;
        printf("shmq_pop:%p\n", cli);
        buf = (char*)msg + sizeof(shm_msg);
        cli->sendbuf = sdscatlen(cli->sendbuf, buf, len - sizeof(shm_msg));
        if (ae_create_file_event(el, cli->fd, AE_WRITABLE, 
                write_to_client, cli) == AE_ERR) {
            free_client(cli);
        }
    }
}

void conn_process_cycle(const void *data) {
    int notifier = notifier_read_fd();
    vb_process = VB_PROCESS_CONN;
    clients = dlist_init();
    ael = ae_create_event_loop();
    listen_fd = anet_tcp_server(sock_error, "0.0.0.0", 5986);
    if (listen_fd == ANET_ERR) {
        fprintf(stderr, "%s\n", sock_error);
        exit(1);
    }

    ae_create_time_event(ael, 1, server_cron, NULL, NULL);

    if (ae_create_file_event(ael, notifier, AE_READABLE, 
                notifier_handler, NULL) == AE_ERR) {
        fprintf(stderr, "ae_create_file_event failed\n");
        exit(1);
    }

    if (listen_fd > 0 && ae_create_file_event(ael, listen_fd, 
                AE_READABLE, accept_handler, NULL) == AE_ERR) {
        fprintf(stderr, "ae_create_file_event failed\n");
        exit(1);
    }

    ae_main(ael);
    ae_free_event_loop(ael);
    exit(0);
}
