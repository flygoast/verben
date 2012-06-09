#include <unistd.h>
#include "conn.h"
#include "sds.h"
#include "anet.h"
#include "shmq.h"
#include "ae.h"

#define IOBUF_SIZE      4096
#define MAX_PROT_LEN    4096

extern shmq_t *recv_queue;
extern shmq_t *send_queue;
static ae_event_loop    el;
static int              listen_fd;
static char sock_error[ANET_ERR_LEN];
static dlist            *clients; 

static int server_cron(ae_event_loop *el, long long id, void *privdate) {
    return 100;
}

static void add_client_conn(int cli_fd) {
    client_conn *cli = malloc(sizeof(*cli));
    assert(cli);
    anet_nonblock(sock_error, cli_fd);
    anet_tcp_nodelay(sock_error, cli_fd);

    if (ae_create_file_event(el, cli_fd, AE_READABLE,
            read_from_client, cli) == AE_ERR) {
        close(cli_fd);
        free(cli);
    }

    cli->fd = cli_fd;
    cli->recv_prot_len = 0;
    dlist_add_node_tail(clients, cli);
}

static void free_client(client_conn *cli) {
    dlist_node *ln;
    ae_delete_file_event(el, cli->fd, AE_READABLE);
    ae_delete_file_event(el, cli->fd, AE_WRITABLE);

    close(cli->fd);
    ln = dlist_search_key(clients, cli);
    dlist_del_node(el, ln);
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

    cli->recv_buf = sdscatlen(cli->recv_buf, buf, nread);
    
    /* The plugin should definite the `handle_input` to process
       the network protocol. */
    if (cli->recv_prot_len == 0) { /* unknown protocol length */
        cli->recv_prot_len = dll.handle_input(cli);
    }

    if (cli->recv_prot_len < 0 || cli->recv_prot_len > MAX_PROT_LEN) {
        /* invalid protocol length */
        free_client(cli);
    } else if (cli->recv_prot_len == 0) {
        /* unknown protocol length */
        /* process big protocol */
    } else if (sdslen(cli->recv_buf) >= cli->recv_prot_len) {
        /* integrity protocol. We'll put the entire datagram into
         * shared memory queue to feed the worker processes. */
        shm_msg msg;
        sds temp;
        msg.cli = cli;
        memcpy(&msg.sk, &cli->sk, sizeof(sock_info));
        msg.accept_fd = cli->accept_fd;
        memcpy(msg.data, cli->recv_buf, cli->recv_prot_len);

        if (shmq_push(recv_queue, &msg, 
                    sizeof(msg) + recv_prot_len, 0) != 0) {
            free_client(cli);
        }

        /* Process the left buffer. */
        assert(temp = sdsrange(cli->recv_buf, cli->prev_prot_len, -1));
        sdsfree(cli->recv_buf);
        cli->recv_buf = 
    }
}

static void write_to_client(ae_event_loop *el, int fd, 
        void *privdata, int mask) {
    client_cli *cli = (client_cli*)privdata;
    int nwrite;
    AE_NOTUSED(el);
    AE_NOTUSED(mask);

    nwrite = write(fd, cli->send_buf, sdslen(cli->send_buf));
    if (nwrite < 0) {
        if (errno == EAGAIN) {
            nwrite = 0;
        } else {
            free_client(cli);
            return;
        }
    }

    if (nwrite == sdslen(cli->send_buf)) {
        ae_delete_file_event(el, cli->fd, AE_WRITABLE);
        sdsclear(cli->send_buf);
    } else {
        /* process the left buffer */
    }
}

static void accept_common_handler(int cli_fd) {
    /* TODO */
    /* Add filter and limit. */
    if (add_client_conn(int cli_fd) < 0) {
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

    cli_fd = anet_tcp_accept(sock_error, fd, cip, &cli_port);
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
    while (shmq_pop(send_queue, &msg, &len, 0) == 0) {
        cli = msg->cli;
        buf = (char*)msg + sizeof(shm_msg);
        sdscatlen(cli->send_buf, buf, len - sizeof(shm_msg));
        if (ae_create_file_event(el, cli->fd, AE_WRITABLE, 
                write_to_client, cli) == AE_ERR) {
            free_client(cli);
        }
    }
}

void conn_process_cycle(const void *data) {
    int notifier = notifier_read_fd();
    clients = dlist_init();
    el = ae_create_event_loop();
    listen_fd = anet_tcp_server(sock_error, "0.0.0.0", "5986");
    if (listen_fd == ANET_ERR) {
        fprintf(stderr, "%s\n", sock_error);
        exit(1);
    }

    ae_create_time_event(el, 1, server_cron, NULL, NULL);

    if (listen_fd > 0 && ae_create_file_event(el, listen_fd, 
                AE_READABLE, accept_handler, NULL) == AE_ERR) {
        fprintf(stderr, "ae_create_file_event failed\n");
        exit(1);
    }

    if (ae_create_file_event(el, notifier, AE_READABLE, 
                notifier_handler, NULL) == AE_ERR) {
        fprintf(stderr, "ae_create_file_event failed\n");
        exit(1);
    }

    ae_main(el);
    ae_delete_event_loop(el);
    exit(0);
}
