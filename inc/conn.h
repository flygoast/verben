#ifndef __CONN_H_INCLUDED__
#define __CONN_H_INCLUDED__

#include "ae.h"

typedef struct client_conn {
    int     fd;
    int     close_conn; /* whether close connection after send response */
    int     refcount;   /* number of messages havn't been processed */ 
    char    *remote_ip;
    int     remote_port;
    int     recv_prot_len;
    char    *sendbuf;
    char    *recvbuf;
    time_t  access_time;
} client_conn;

typedef struct shm_msg {
    client_conn *cli;
    char        remote_ip[16];
    int         remote_port;
    int         close_conn;
    char        data[0];
} __attribute__((packed)) shm_msg;

extern ae_event_loop    *ael;
void conn_process_cycle(void *data);

#endif /* __CONN_H_INCLUDED__ */
