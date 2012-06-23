#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "anet.h"

#define ANET_CONNECT_NONE       0
#define ANET_CONNECT_NONBLOCK   1

static void anet_set_error(char *err, const char *fmt, ...) {
    va_list ap;
    if (!err) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(err, ANET_ERR_LEN, fmt, ap);
    va_end(ap);
}

static int anet_create_socket(char *err, int domain) {
    int s, on = 1;
    if ((s = socket(domain, SOCK_STREAM, 0)) == -1) {
        anet_set_error(err, "create socket failed: %s", strerror(errno));
        return ANET_ERR;
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        anet_set_error(err, "setsockopt SO_REUSEADDR: %s", 
            strerror(errno));
        return ANET_ERR;
    }
    return s;
}

static int anet_tcp_generic_connect(char *err, char *addr, 
        int port, int flags) {
    int sockfd;
    struct sockaddr_in sa;

    if ((sockfd = anet_create_socket(err, AF_INET)) == ANET_ERR) {
        return ANET_ERR;
    }

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_aton(addr, &sa.sin_addr) == 0) {
        struct hostent *he;
        he = gethostbyname(addr);
        if (he == NULL) {
            anet_set_error(err, "resolve %s failed: %s", 
                addr, hstrerror(h_errno));
            close(sockfd);
            return ANET_ERR;
        }
        memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
    }

    if (flags & ANET_CONNECT_NONBLOCK) {
        if (anet_nonblock(err, sockfd) != ANET_OK) {
            return ANET_ERR;
        }
    }

    if (connect(sockfd, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        if (errno == EINPROGRESS &&
                flags & ANET_CONNECT_NONBLOCK) {
            return sockfd;
        }
        anet_set_error(err, "connect %s failed: %s", 
            addr, strerror(errno));
        close(sockfd);
        return ANET_ERR;
    }
    return sockfd;
}

static int anet_listen(char *err, int sockfd, struct sockaddr *sa,
        socklen_t len) {
    if (bind(sockfd, sa, len) == -1) {
        anet_set_error(err, "bind failed: %s", strerror(errno));
        close(sockfd);
        return ANET_ERR;
    }

    /* The magic 511 constant is from nginx. */
    if (listen(sockfd, 511) == -1) {
        anet_set_error(err, "listen failed:%s", strerror(errno));
        close(sockfd);
        return ANET_ERR;
    }
    return ANET_OK;
}

static int anet_unix_generic_connect(char *err, char *path, int flags) {
    int sockfd;
    struct sockaddr_un sa;
    if ((sockfd = anet_create_socket(err, AF_LOCAL)) == ANET_ERR) {
        return ANET_ERR;
    }

    sa.sun_family = AF_LOCAL;
    strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
    if (flags & ANET_CONNECT_NONBLOCK) {
        if (anet_nonblock(err, sockfd) != ANET_OK) {
            return ANET_ERR;
        }
    }

    if (connect(sockfd, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        if (errno == EINPROGRESS &&
                flags & ANET_CONNECT_NONBLOCK) {
            return sockfd;
        }
        anet_set_error(err, "connect %s failed:%s", 
            path, strerror(errno));
        close(sockfd);
        return ANET_ERR;
    }
    return sockfd;
}

static int anet_generic_accept(char *err, int sockfd, 
        struct sockaddr *sa, socklen_t *len) {
    int fd;
    while (1) {
        fd = accept(sockfd, sa, len);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                anet_set_error(err, "accept failed:%s", strerror(errno));
                return ANET_ERR;
            }
        }
        break;
    }
    return fd;
}

int anet_nonblock(char *err, int fd) {
    int flags;

    /* Set the socket nonblocking. Note that fcntl(2) for F_GETFL
       and F_SETFL can't be interrupted by a signal. */
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        anet_set_error(err, "fcntl(F_GETFL) failed: %s", strerror(errno));
        return ANET_ERR;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        anet_set_error(err, "fcntl(F_SETFL, O_NONBLOCK) failed: %s",
            strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anet_tcp_nodelay(char *err, int fd) {
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, 
            sizeof(yes)) == -1) {
        anet_set_error(err, "setsockopt TCP_NODELAY failed: %s",
            strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anet_set_send_buffer(char *err, int fd, int buffsize) {
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, 
            &buffsize, sizeof(buffsize)) == -1) {
        anet_set_error(err, "setsockopt SO_SNDBUF failed: %s",
            strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anet_tcp_keepalive(char *err, int fd) {
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
            &yes, sizeof(yes)) == -1) {
        anet_set_error(err, "setsockopt SO_KEEPALIVE failed: %s",
            strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

int anet_resolve(char *err, char *host, char *ipbuf) {
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    if (inet_aton(host, &sa.sin_addr) == 0) {
        struct hostent *he;
        he = gethostbyname(host);
        if (he == NULL) {
            anet_set_error(err, "resolve %s failed: %s", 
                host, hstrerror(h_errno));
            return ANET_ERR;
        }
        memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
    }
    strcpy(ipbuf, inet_ntoa(sa.sin_addr));
    return ANET_OK;
}

int anet_tcp_connect(char *err, char *addr, int port) {
    return anet_tcp_generic_connect(err, addr, port, ANET_CONNECT_NONE);
}

int anet_tcp_nonblock_connect(char *err, char *addr, int port) {
    return anet_tcp_generic_connect(err, addr, port, 
        ANET_CONNECT_NONBLOCK);
}

int anet_unix_connect(char *err, char *path) {
    return anet_unix_generic_connect(err, path, ANET_CONNECT_NONE);
}

int anet_unix_nonblock_connect(char *err, char *path) {
    return anet_unix_generic_connect(err, path, ANET_CONNECT_NONBLOCK);
}

/* Like read(2) but make sure 'count' is read before to return, 
   unless error or EOF condition is encountered. */
int anet_read(int fd, char *buf, int count) {
    int nread, totlen = 0;
    while (totlen != count) {
        nread = read(fd, buf, count - totlen);
        if (nread == 0) {
            return totlen; /* EOF */
        }

        if (nread < 0) {
            return -1;
        }
        totlen += nread;
        buf += nread;
    }
    return totlen;
}

/* Like write(2) but make sure 'count' is write before to return,
   unless error is encountered. */
int anet_write(int fd, char *buf, int count) {
    int nwritten, totlen = 0;
    while (totlen != count) {
        nwritten = write(fd, buf, count - totlen);
        if (nwritten == 0) {
            return totlen;
        }
        if (nwritten < 0) {
            return -1;
        }
        totlen += nwritten;
        buf += nwritten;
    }
    return totlen;
}

int anet_tcp_server(char *err, char *addr, int port) {
    int sockfd;
    struct sockaddr_in sa;
    if ((sockfd = anet_create_socket(err, AF_INET)) == ANET_ERR) {
        return ANET_ERR;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (addr && inet_aton(addr, &sa.sin_addr) == 0) {
        anet_set_error(err, "invalid bind address");
        close(sockfd);
        return ANET_ERR;
    }

    if (anet_listen(err, sockfd, (struct sockaddr*)&sa, sizeof(sa))
            == ANET_ERR) {
        return ANET_ERR;
    }
    return sockfd;
}

int anet_unix_server(char *err, char *path, mode_t perm) {
    int sockfd;
    struct sockaddr_un sa;

    if ((sockfd = anet_create_socket(err, AF_LOCAL)) == ANET_ERR) {
        return ANET_ERR;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_LOCAL;
    strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
    if (anet_listen(err, sockfd, (struct sockaddr*)&sa, sizeof(sa)) 
            == ANET_ERR) {
        return ANET_ERR;
    }
    if (perm) {
        chmod(sa.sun_path, perm);
    }
    return sockfd;
}

int anet_tcp_accept(char *err, int sockfd, char *ip, int *port) {
    int fd;
    struct sockaddr_in sa;
    socklen_t salen = sizeof(sa);
    if ((fd = anet_generic_accept(err, sockfd, 
            (struct sockaddr*)&sa, &salen)) == ANET_ERR) {
        return ANET_ERR;
    }

    if (ip) {
        strcpy(ip, inet_ntoa(sa.sin_addr));
    }

    if (port) {
        *port = ntohs(sa.sin_port);
    }
    return fd;
}

int anet_unix_accept(char *err, int sockfd) {
    int fd;
    struct sockaddr_un sa;
    socklen_t salen = sizeof(sa);
    if ((fd = anet_generic_accept(err, sockfd, 
            (struct sockaddr*)&sa, &salen)) == ANET_ERR) {
        return ANET_ERR;
    }
    return fd;
}

int anet_peer_tostring(char *err, int fd, char *ip, int *port) {
    struct sockaddr_in sa;
    socklen_t salen = sizeof(sa);

    if (getpeername(fd, (struct sockaddr*)&sa, &salen) == -1) {
        anet_set_error(err, "getpeername failed:%s", 
            strerror(errno));
        return ANET_ERR;
    }

    if (ip) {
        strcpy(ip, inet_ntoa(sa.sin_addr));
    }

    if (port) {
        *port = ntohs(sa.sin_port);
    }
    return ANET_OK;
}

#ifdef ANET_TEST_MAIN
#include <stdlib.h>
#include <sys/select.h>

int main(void) {
    fd_set rfds;
    char error[ANET_ERR_LEN];
    char msg[] = "hello,world\r\n";
    char ip[INET_ADDRSTRLEN];
    struct timeval timeout;
    int ready;
    int port;
    int fd;
    int sock;

    if (anet_resolve(error, "localhost", ip) == ANET_ERR) {
        fprintf(stderr, "%s\n", error);
        exit(1);
    }
    printf("LOCAL:%s\n", ip);

    if ((fd = anet_tcp_server(error, ip, 5986)) == ANET_ERR) {
        fprintf(stderr, "%s\n", error);
        exit(1);
    }

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        if ((ready = select(fd + 1, &rfds, NULL, NULL, &timeout)) 
                == -1 && errno != EINTR) {
            perror("select");
            exit(1);
        }

        if (ready > 0) {
            if (FD_ISSET(fd, &rfds)) {
                int sock;
                struct sockaddr_in sockaddr;
                socklen_t socklen;

                socklen = sizeof(sockaddr);
                memset(&sockaddr, 0, socklen);
                if ((sock = accept(fd, 
                    (struct sockaddr *)&sockaddr, &socklen)) != -1) {
                    if (anet_peer_tostring(error, sock, ip, &port)
                            == ANET_ERR) {
                        fprintf(stderr, "%s\n", error);
                        continue;
                    }
                    printf("%s:%d\n", ip, port);
                    write(sock, msg, strlen(msg));
                    close(sock);
                }
            }
        }
    }
    exit(0);
}
#endif /* ANET_TEST_MAIN */
