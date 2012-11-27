#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "verben.h"
#include "version.h"
#include "plugin.h"
#include "log.h"

void __echo_plugin_main(void) {
    printf("** verben [echo] plugin **\n");
    printf("Copyright(c)flygoast, flygoast@126.com\n");
    printf("verben version: %s\n", VERBEN_VERSION);
    exit(0);
}

int handle_init(void *conf, int proc_type) {
    switch (proc_type) {
        case VB_PROCESS_MASTER:
            return 0;
        case VB_PROCESS_WORKER:
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
    return len;
}

int handle_process(char *rcvbuf, int rcvlen, 
        char **sndbuf, int *sndlen, const char *remote_ip, int port) {
    *sndbuf = (char *)malloc(rcvlen);
    *sndlen = rcvlen;

    memcpy(*sndbuf, rcvbuf, rcvlen);
    return VERBEN_OK;
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
