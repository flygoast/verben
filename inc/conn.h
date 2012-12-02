#ifndef __CONN_H_INCLUDED__
#define __CONN_H_INCLUDED__

#include <unistd.h>
#include "ae.h"

#define VERBEN_OK               0x00000000
#define VERBEN_ERROR            0x00000001
#define VERBEN_CONN_CLOSE       0x00000002

#define CONN_MSG_MAGIC          0x567890EF
#define CONN_MAGIC_DEBUG        0x1234ABCD

typedef struct client_conn {
#ifdef DEBUG
    int     magic;
#endif /* DEBUG */
    int     fd;
    int     close_conn; /* whether close connection after send response */
    char    *remote_ip;
    int     remote_port;
    int     recv_prot_len;
    char    *sendbuf;
    char    *recvbuf;
    time_t  access_time;
} client_conn;

typedef struct shm_msg {
    client_conn     *cli;
    int             fd;
#ifdef DEBUG
    unsigned int    identi;
    unsigned int    magic; /* the field just to protect `cli`'s usage to
                              avoid core dump upon some error. */
#endif /* DEBUG */
    pid_t           pid; /* the field used to check whethe the receiving 
                        * conn process is a new conn process. */
    char            remote_ip[16];
    int             remote_port;
    int             close_conn;
    char            data[0];
} __attribute__((packed)) shm_msg;

extern ae_event_loop    *ael;
void conn_process_cycle(void *data);

#endif /* __CONN_H_INCLUDED__ */
