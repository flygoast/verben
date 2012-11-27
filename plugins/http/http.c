/* This is a very simple http server. Just as a verben plugin sample. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include "verben.h"
#include "version.h"
#include "plugin.h"
#include "log.h"
#include "conf.h"

#define HTTP_METHOD_GET     0
#define HTTP_METHOD_PUT     1
#define HTTP_METHOD_POST    2
#define HTTP_METHOD_HEAD    3

#define RESPONSE_BUF_SIZE   4096

void __http_plugin_main(void) {
    printf("** verben [http] plugin **\n");
    printf("Copyright(c)flygoast, flygoast@126.com\n");
    printf("verben version: %s\n", VERBEN_VERSION);
    exit(0);
}

static char *doc_root;
static char *index_file;

int handle_init(void *cycle, int proc_type) {
    conf_t *conf = (conf_t*)cycle;
    switch (proc_type) {
        case VB_PROCESS_MASTER:
            return 0;
        case VB_PROCESS_WORKER:
            doc_root = conf_get_str_value(conf, "docroot", 
                    "/home/flygoast");
            index_file = conf_get_str_value(conf, "index", "index.html");
            return 0;
        case VB_PROCESS_CONN:
            return 0;
    }

    /* Never get here. */
    return -1;
}

/* This API implementation is optional. */
/*
int handle_open(char **sendbuf, int *len, 
        const char *remote_ip, int port) {
    char *msg = "welcome to verben\n";
    char *buf = (char *)malloc(strlen(msg) + 1);

    strcpy(buf, msg);
    *sendbuf = buf;
    *len = strlen(msg) + 1;
    DEBUG_LOG("Receive a connection from %s:%d", remote_ip, port);
    return 0;
}
*/


void handle_close(const char *remote_ip, int port) {
    DEBUG_LOG("Connection from %s:%d closed", remote_ip, port);
}

int handle_input(char *buf, int len, const char *remote_ip, int port) {
    /* At here, try to find the end of the http request. */
    int content_len;
    char *ptr;
    char *header_end;
    if (!(header_end = strstr(buf, "\r\n\r\n"))) {
        return 0;
    }
    header_end += 4;
    /* find content-length header */
    if ((ptr = strstr(buf, "Content-Length:"))) {
        content_len = strtol(ptr + strlen("Content-Length:"), NULL, 10);
        if (content_len == 0) {
            ERROR_LOG("Invalid http protocol: %s", buf);
            return -1;
        }
        return header_end - buf + content_len;
    } else {
        return header_end - buf;
    }
}

int handle_process(char *rcvbuf, int rcvlen, 
        char **sndbuf, int *sndlen, const char *remote_ip, int port) {
    /* Parse request and generate response. */
    int n = 0;
    int file_size;
    char *message;
    char *ptr;
    char *ptr2;
    char file[256] = {};
    int fd;
    
    /* copy the message, because of the recvbuf is not null-terminated. */
    message = (char *)malloc(rcvlen + 1);
    memcpy(message, rcvbuf, rcvlen);
    message[rcvlen] = '\0';

    ptr = message;
    if (!strncmp(ptr, "GET", 3)) {  /* Only support method GET */
        ptr += 4;
    } else {
        /* TODO unsupported methods */
        return VERBEN_ERROR;
    }

    if (!(ptr2 = strstr(ptr, "HTTP/"))) {
        return VERBEN_ERROR;
    }

    *--ptr2 = '\0';
    
    /* Generate filename accessed */
    snprintf(file, sizeof(file), "%s/%s", doc_root, 
            (ptr[strlen(ptr) - 1] == '/') ? 
            index_file : ptr);
    free(message);

    fd = open(file, O_RDONLY);
    if (fd < 0) {
        ERROR_LOG("open file [%s] failed", file);
        return VERBEN_ERROR;
    }
    file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    *sndbuf = (char *)malloc(RESPONSE_BUF_SIZE);
    ptr = *sndbuf;
    ptr2 = *sndbuf + RESPONSE_BUF_SIZE;
    ptr += snprintf(ptr, ptr2 - ptr - 1, 
            "HTTP/1.1 200 OK\r\nServer: verben " VERBEN_VERSION "\r\n"
            "Content-Length: %d\r\n\r\n", file_size);
    while ((n = read(fd, ptr, ptr2 - ptr -1)) > 0) {
        DEBUG_LOG("Read n=%d, buffer:%s", n, ptr);
        ptr += n;
    }
    *ptr = '\0';
    *sndlen = ptr - *sndbuf;
    close(fd);
    if (n < 0) {
        return VERBEN_ERROR;
    }
    return VERBEN_CONN_CLOSE;
}

/* This function used to free the memory allocated in handle_process().
 * It is NOT mandatory. */
void handle_process_post(char *sendbuf, int sendlen) {
    if (sendbuf) {
        free(sendbuf);
    }
}

void handle_fini(void *cycle, int proc_type) {
    switch (proc_type) {
        case VB_PROCESS_MASTER:
            break;
        case VB_PROCESS_WORKER:
            break;
        case VB_PROCESS_CONN:
            break;
    }
}
