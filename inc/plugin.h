#ifndef __PLUGIN_H_INCLUDED__
#define __PLUGIN_H_INCLUDED__
#include <sys/cdefs.h>
#include "conn.h"

__BEGIN_DECLS

#define VERBEN_OK               0x00000000
#define VERBEN_ERROR            0x00000001
#define VERBEN_CONN_CLOSE       0x00000002
#define VERBEN_CONN_KEEPALIVE   0x00000004

/* It's optional. If implemented, it would be invoked when
 * the process at beginning phase. You should do some 
 * initializetion work in it. When success, please ruturn
 * 0, otherwise -1 should be returned. Upon failure, the
 * verben daemon will exit immediately. */
int handle_init(void *cycle, int proc_type);

/* It's optional. If implemented, it would be invoked when
 * the process would exit. You should do some destructation
 * work in it. */
void handle_fini(void *cycle, int proc_type);

/* It's optional.  When a connection accepted, the `handle_open' 
 * would be invoked. The `sendbuf' and 'len' can be used to write
 * some message to client upon connection. The implementation of 
 * this function should allocate memory from heap, and return
 * the address in `sendbuf' and its length in 'len'. Otherwise,
 * set `sendbuf' to NULL. When success, please ruturn 0, otherwise,
 * the connection will be closed. */
int handle_open(char **sendbuf, int *len, const char *remote_ip, int port);

/* It's optinal. When a connection closed, it would be invoked. */
void handle_close(const char *remote_ip, int port);

/* This function is mandatory. Your plugin MUST implemente this 
 * function. This function should return the length of a protocal
 * message. When it's unknown, return 0. You also can return -1
 * to close this connection. */
int handle_input(char*recvbuf, int recvlen, 
        const char *remote_ip, int port);

/* This function is mandatory. Your plugin MUST implement it. 
 * The protocol message mainly was processed in it. */
int handle_process(char *recvbuf, int recvlen, 
        char **sendbuf, int *sendlen, const char *remote_ip, int port);

/* Add interp section just for geek.
 * This partion of code can be remove to Makefile 
 * to determine RTLD(runtime loader). */
#if __WORDSIZE == 64
#define RUNTIME_LINKER  "/lib64/ld-linux-x86-64.so.2"
#else
#define RUNTIME_LINKER  "/lib/ld-linux.so.2"
#endif

const char __invoke_dynamic_linker__[] __attribute__ ((section (".interp")))
    = RUNTIME_LINKER;

__END_DECLS
#endif /* __PLUGIN_H_INCLUDED__ */
