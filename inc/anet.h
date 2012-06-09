#ifndef __ANET_H_INCLUDED__
#define __ANET_H_INCLUDED__

#define ANET_OK     0
#define ANET_ERR    -1
#define ANET_ERR_LEN    256

extern int anet_tcp_connect(char *err, char *addr, int port);
extern int anet_tcp_nonblock_connect(char *err, char *addr, int port);
extern int anet_unix_connect(char *err, char *path);
extern int anet_unix_nonblock_connect(char *err, char *path);
extern int anet_read(int fd, char *buf, int count);
extern int anet_resolve(char *err, char *host, char *ipbuf);
extern int anet_tcp_server(char *err, char *bindaddr, int port);
extern int anet_unix_server(char *err, char *path, mode_t perm);
extern int anet_tcp_accept(char *err, int serversock, 
        char *ip, int *port);
extern int anet_unix_accept(char *err, int serversock);
extern int anet_write(int fd, char *buf, int count);
extern int anet_nonblock(char *err, int fd);
extern int anet_tcp_nodelay(char *err, int fd);
extern int anet_tcp_keepalive(char *err, int fd);
extern int anet_peer_tostring(char *err, int fd, char *ip, int *port);
extern int anet_set_send_buffer(char *err, int fd, int buffsize);

#endif /* __ANET_H_INCLUDED__ */
