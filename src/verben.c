#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <assert.h>

#include "verben.h"
#include "daemon.h"
#include "notifier.h"
#include "shmq.h"
#include "conn.h"
#include "worker.h"
#include "config.h"
#include "dll.h"
#include "log.h"
#include "version.h"

#define PROG_NAME                   "verben"

#define VB_MAX_PROCESSES            1024
#define VB_PROCESS_NORESPAWN        -1
#define VB_PROCESS_JUST_SPAWN       -2
#define VB_PROCESS_RESPAWN          -3
#define VB_PROCESS_JUST_RESPAWN     -4
#define VB_PROCESS_DETACHED         -5

typedef void (*child_proc_t)(const void *);

typedef struct {
    int     signo;
    char    *signame;
    const char *name;
    void    (*handler)(int signo);
} vb_signal_t;

typedef struct {
    pid_t           pid;
    int             status;
    child_proc_t   proc;
    const void      *data;
    const char      *name; 

    unsigned respawn:1;
    unsigned just_spawn:1;
    unsigned detached:1;
    unsigned exiting:1;
    unsigned exited:1;
} vb_process_t;

/* Global variables */
vb_process_t vb_processes[VB_MAX_PROCESSES];
int vb_process;
int vb_process_slot;
int vb_last_process;

shmq_t *recv_queue;
shmq_t *send_queue;

/* for loading so file */
void * handle;
dll_func_t dll;

/* config */
config_t   conf;

sig_atomic_t vb_reap;
sig_atomic_t vb_quit;
sig_atomic_t vb_worker_quit;
static void vb_signal_handler(int signo);

vb_signal_t signals[] = {
    {SIGTERM, "SIGTERM", "", vb_signal_handler},
    {SIGQUIT, "SIGQUIT", "", vb_signal_handler},
    {SIGCHLD, "SIGCHLD", "", vb_signal_handler},
    {SIGPIPE, "SIGPIPE, SIG_IGN", "", SIG_IGN},
    {SIGINT, "SIGINT", "", SIG_IGN},
    {0, NULL, "", NULL}
};

static void init_vb_processes() {
    int i = 0;
    for (i = 0; i < VB_MAX_PROCESSES; ++i) {
        vb_processes[i].pid = -1;
    }
}

static void process_get_status(void) {
    int     status;
    const char *process;
    int     i;
    pid_t   pid;
    int     one = 0;

    for ( ; ; ) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid == 0) {
            fprintf(stderr, "xxxxxxxxxxxxxx\n");
            return;
        }
        printf("pid=%d\n", pid);

        if (pid == -1) {
            if (errno == EINTR) {
                continue;
            }

            /* for what */
            if (errno == ECHILD && one) {
                return;
            }
            fprintf(stderr, "oooooooooooo\n");
            return;
        }
        one = 1;

        printf("vb_task_process=%d\n", vb_last_process);
        for (i = 0; i < vb_last_process; ++i) {
            printf("pid = %d, vb_processes[i].pid=%d\n", 
                pid, vb_processes[i].pid);
            if (vb_processes[i].pid == pid) {
                vb_processes[i].status = status;
                vb_processes[i].exited = 1;
                break;
            }
        }
    }
}

static void vb_signal_handler(int signo) {
    vb_signal_t     *sig;

    /* Get the current signal. */
    for (sig = signals; sig->signo != 0; ++sig) {
        if (sig->signo == signo) {
            break;
        }
    }

    switch (vb_process) {
    case VB_PROCESS_MASTER:
        switch (signo) {
        case SIGQUIT: /* prepare for shutting down */
            vb_quit = 1;
            break;
        case SIGTERM:
            vb_quit = 1;
            break;

        case SIGCHLD:
            vb_reap = 1;
            break;
        }
        break;
    case VB_PROCESS_WORKER:
        switch (signo) {
        case SIGTERM:
            vb_worker_quit = 1;
            break;
        }
        break;
    case VB_PROCESS_CONN:
        switch (signo) {
        case SIGTERM:
            ae_stop(ael);
            break;
        }
        break;
    }

    if (signo == SIGCHLD) {
        process_get_status();
    }
}

static int init_signals() {
    vb_signal_t *sig;
    struct sigaction sa;
    
    for (sig = signals; sig->signo != 0; ++sig) {
        memset(&sa, 0, sizeof(struct sigaction));
        sa.sa_handler = sig->handler;
        sigemptyset(&sa.sa_mask);
        if (sigaction(sig->signo, &sa, NULL) == -1) {
            return -1;
        }
    }
    return 0;
}

static pid_t spawn_process(child_proc_t proc, const void *data, 
        const char *name, int respawn) {
    unsigned long on;
    int s;
    pid_t pid;

    if (respawn >= 0) {
        /* We'll respawn a exited process whose slot is 'respawn'. */
        s = respawn;
    } else {
        /* Iterate the vb_processes to find a available slot. */
        for (s = 0; s < vb_last_process; ++s) {
            if (vb_processes[s].pid == -1) {
                break;
            }
        }

        /* Get the limit value of process number. */
        if (s == VB_MAX_PROCESSES) {
            return -1;
        }
    }

    /* Set the slot position in the 'vb_processes' table. */
    vb_process_slot = s;

    pid = fork();
    switch (pid) {
    case -1:
        return -1;
    case 0: /* child process */
        daemon_set_title(name);
        proc(data);
        break;
    default:
        break;
    }

    vb_processes[s].pid = pid;
    vb_processes[s].exited = 0;

    if (respawn >= 0) {
        return pid;
    }

    /* Initialize process structure. */
    vb_processes[s].proc = proc;
    vb_processes[s].data = data;
    vb_processes[s].name = name;
    vb_processes[s].exiting = 0;
    switch (respawn) {
    case VB_PROCESS_NORESPAWN:
        vb_processes[s].respawn = 0;
        vb_processes[s].just_spawn = 0;
        vb_processes[s].detached = 0;
        break;
    case VB_PROCESS_JUST_SPAWN:
        vb_processes[s].respawn = 0;
        vb_processes[s].just_spawn = 1;
        vb_processes[s].detached = 0;
        break;
    case VB_PROCESS_RESPAWN:
        vb_processes[s].respawn = 1;
        vb_processes[s].just_spawn = 0;
        vb_processes[s].detached = 0;
        break;
    case VB_PROCESS_JUST_RESPAWN:
        vb_processes[s].respawn = 1;
        vb_processes[s].just_spawn = 1;
        vb_processes[s].detached = 0;
        break;
    case VB_PROCESS_DETACHED:
        vb_processes[s].respawn = 0;
        vb_processes[s].just_spawn = 0;
        vb_processes[s].detached = 1;
        break;
    }
    if (s == vb_last_process) {
        ++vb_last_process;
    }

    return pid;
}

static int reap_children() {
    int i;
    int live = 0;
    for (i = 0; i < vb_last_process; ++i) {
        if (vb_processes[i].pid == -1) {
            continue;
        }

        if (vb_processes[i].exited) {
            if (vb_processes[i].respawn &&
                !vb_processes[i].exiting &&
                !vb_quit) {
                if (spawn_process(vb_processes[i].proc, 
                        vb_processes[i].data, 
                        vb_processes[i].name, i) == -1) {
                    continue;
                }
                live = 1;
            }
        } else {
            live = 1;
        }
    }
    return live;
}

static void create_processes(child_proc_t func,
        const void *data, char *proc_name, int n, int type) {
    int i = 0;

    for (i = 0; i < n; ++i) {
        if (spawn_process(func, data, proc_name, type) < 0) {
            continue;
        }
    }
}

static void signal_worker_processes(int signo) {
    int i = 0;
    for (i = 0; i < vb_last_process; ++i) {
        if (vb_processes[i].pid == -1) {
            continue;
        }

        if (!vb_processes[i].exited) {
            kill(vb_processes[i].pid, signo);
        }
    }
}

static void master_process_cycle() {
    int live = 1;
    sigset_t set;
    sigemptyset(&set);

    /* signals to mask before completing spawning 
       the worker processes */
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGQUIT);

    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        fprintf(stderr, "sigprocmask failed\n");
        exit(1);
    }

    /* Create the notifier between worker processes and conn process. */
    if (notifier_create() < 0) {
        BOOT_FAILED("Create notifier between workers and conn process");
    }

    assert(recv_queue = shmq_create(1 << 26));
    assert(send_queue = shmq_create(1 << 26));

    create_processes(conn_process_cycle, NULL, 
            PROG_NAME":[conn]", 1, VB_PROCESS_RESPAWN);
    create_processes(worker_process_cycle, NULL, 
            PROG_NAME":[worker]",
            config_get_int_value(&conf, "worker_num", 4),
            VB_PROCESS_RESPAWN);

    /* Close notifier between workers and conn process. */
    notifier_close_rd();
    notifier_close_wr();

    sigemptyset(&set);
    for ( ; ; ) {
        sigsuspend(&set);

        if (vb_reap) {
            vb_reap = 0;
            live = reap_children();
        }

        if (!live && vb_quit) {
            printf("Master process will exit!\n");
            if (dll.handle_fini) {
                dll.handle_fini(NULL, vb_process);
            }
            exit(0);
        }

        if (vb_quit) {
            if (kill(0, SIGTERM) < 0) {
                fprintf(stderr, "kill all children failed\n");
            }
            continue;
        }
    }
}

/*---------------------------- main ---------------------------*/
int main(int argc, char *argv[]) {
    char **saved_argv;
    char *so_name = NULL;

    symbol_t syms[] = {
        /* symbol_name,     function pointer,       optional */
        {"handle_init",     (void **)&dll.handle_init,       1},
        {"handle_fini",     (void **)&dll.handle_fini,       1},
        {"handle_open",     (void **)&dll.handle_open,       1},
        {"handle_close",    (void **)&dll.handle_close,      1},
        {"handle_input",    (void **)&dll.handle_input,      0},
        {"handle_process",  (void **)&dll.handle_process,    0},
        {NULL, NULL, 0}
    };
    vb_process = VB_PROCESS_MASTER;

    printf(PROG_NAME": A network server bench.\n"
        "  version: %s\n"
        "  Copyright (c) flygoast, flygoast@126.com\n\n",
        VERBEN_VERSION);

    init_vb_processes();
    BOOT_OK("Initialize process structure");
    /* TODO: getopt */
    
    /* Parse config file. */
    if (config_init(&conf, argv[1]) != 0) {
        BOOT_FAILED("Load config file %s", argv[1]);
    }
    BOOT_OK("Load config file %s", argv[1]);

    if (init_signals() < 0) {
        BOOT_FAILED("Initialize signal handlers");
    }
    BOOT_OK("Initialize signal handlers");

    if (log_init(config_get_str_value(&conf, "log_dir", "."),
            config_get_str_value(&conf, "log_name", "taskbench.log"),
            config_get_int_value(&conf, "log_level", LOG_LEVEL_ALL),
            config_get_int_value(&conf, "log_size", LOG_FILE_SIZE),
            config_get_int_value(&conf, "log_num", LOG_FILE_NUM),
            config_get_int_value(&conf, "log_multi", LOG_MULTI_NO)) < 0) {
        BOOT_FAILED("Initialize log file");
    }
    BOOT_OK("Initialize log file");

    /* Make the process to be the leader process of the group */
    if (setpgrp() < 0) {
        BOOT_FAILED("Set self to be leader of the process group");
    }
    BOOT_OK("Set self to be leader of the process group");

    /* load .so file */
    so_name = config_get_str_value(&conf, "so_file", NULL);
    if (load_so(&handle, syms, so_name) < 0) {
        BOOT_FAILED("load so file %s", so_name ? so_name : "(NULL)");
    }
    BOOT_OK("load so file %s", so_name ? so_name : "(NULL)");

    if (dll.handle_init) {
        if (dll.handle_init(NULL, vb_process) != 0) {
            BOOT_FAILED("handle_init");
        }
    }

#ifdef DEBUG
    printf("Start...\n");
#endif /* DEBUG */

    saved_argv = daemon_argv_dup(argc, argv);
    if (!saved_argv) {
        BOOT_FAILED("daemonized");
    }

    daemon_set_title("%s:[master]", PROG_NAME);
    master_process_cycle();
    exit(0);
}
