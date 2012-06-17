#ifndef __CONN_H_INCLUDED__
#define __CONN_H_INCLUDED__

#include "ae.h"

typedef struct sock_info {
    int         sockfd;
    int         type;
    long long   recv_tm;
    long long   send_tm;
    u_int       local_ip;
    u_short     local_port;
    u_int       remote_ip;
    u_short     remote_port;
} sock_info;

typedef struct client_conn {
    int     fd;
    int     recv_prot_len;
    int     accept_fd;
    char    *sendbuf;
    char    *recvbuf;
    sock_info   sk;
} client_conn;

typedef struct shm_msg {
    client_conn *cli;
    sock_info   sk;
    u_int       accept_fd;
    char        data[0];
} __attribute__((packed)) shm_msg;


extern ae_event_loop    *ael;
void conn_process_cycle(void *data);

#endif /* __CONN_H_INCLUDED__ */
