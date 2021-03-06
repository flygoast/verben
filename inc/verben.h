#ifndef __VERBEN_H_INCLUDED__
#define __VERBEN_H_INCLUDED__

#include <signal.h>
#include "conn.h"
#include "conf.h"
#include "dll.h"
#include "shmq.h"

#define VB_PROCESS_MASTER   0
#define VB_PROCESS_WORKER   1
#define VB_PROCESS_CONN     2

typedef struct dll_func_struct {
    int (*handle_init)(void *, int);
    void (*handle_fini)(void *, int);
    int (*handle_open)(char **, int *, char *, int);
    void (*handle_close)(char *, int);
    int (*handle_input)(const char*, int, char *, int);
    int (*handle_process)(char *, int, char **, int *, char *, int);
    int (*handle_process_post)(char *, int);
} dll_func_t;

extern int vb_process;
extern shmq_t *recv_queue;
extern shmq_t *send_queue;
extern dll_func_t dll;
extern sig_atomic_t vb_worker_quit;
extern sig_atomic_t vb_quit;

#endif /* __VERBEN_H_INCLUDED__ */
