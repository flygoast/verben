#ifndef __PLUGIN_H_INCLUDED__
#define __PLUGIN_H_INCLUDED__
#include <sys/cdefs.h>
#include "conn.h"

__BEGIN_DECLS

int handle_init(void *cycle, int proc_type);
void handle_fini(void *cycle, int proc_type);

/* When a connection accepted, the `handle_open' invoked.
   the `sendbuf' and 'len' can be used to write some message
   to client upon connection. The implementation of this 
   function should allocate memory from heap, and return
   the address in `sendbuf' and its length in 'len'. Otherwise,
   set `sendbuf' to NULL. */
int handle_open(char **sendbuf, int *len, const char *remote_ip, int port);

void handle_close(const char *remote_ip, int port);
int handle_input(char*recvbuf, int recvlen, 
        const char *remote_ip, int port);
int handle_process(char *recvbuf, int recvlen, 
        char **sendbuf, int *sendlen, const char *remote_ip, int port);

__END_DECLS
#endif /* __PLUGIN_H_INCLUDED__ */
