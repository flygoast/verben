#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "verben.h"
#include "plugin.h"

int handle_init(void *cycle, int proc_type) {
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

int handle_input(char *buffer, int length, const sock_info *sk) {
    return length;
}

int handle_process(char *rcvbuf, int rcvlen, 
        char **sndbuf, int *sndlen, const sock_info *sk) {
    *sndbuf = (char *)malloc(rcvlen);
    *sndlen = rcvlen;

    memcpy(*sndbuf, rcvbuf, rcvlen);
    return 0;
}
