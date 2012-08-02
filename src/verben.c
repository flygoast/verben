#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <assert.h>
#ifdef __linux__
#define HAVE_BACKTRACE
#endif /* __linux__ */
#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#endif /* HAVE_BACKTRACE */
#include "verben.h"
#include "daemon.h"
#include "notifier.h"
#include "shmq.h"
#include "conn.h"
#include "worker.h"
#include "conf.h"
#include "dll.h"
#include "log.h"
#include "version.h"

#define PROG_NAME                   "verben"
#define PID_FILE                    "/tmp/verben.pid"
#define DAEMON_START                0
#define DAEMON_STOP                 1

#define VB_MAX_PROCESSES            1024
#define VB_PROCESS_NORESPAWN        -1
#define VB_PROCESS_JUST_SPAWN       -2
#define VB_PROCESS_RESPAWN          -3
#define VB_PROCESS_JUST_RESPAWN     -4
#define VB_PROCESS_DETACHED         -5

typedef void (*child_proc_t)(void *);

typedef struct {
    int     signo;
    char    *signame;
    const char *name;
    void    (*handler)(int signo);
} vb_signal_t;

typedef struct {
    pid_t           pid;
    int             status;
    child_proc_t    proc;
    void            *data;
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

int daemon_action;
char *pid_file;

/* for loading so file */
void * handle;
dll_func_t dll;

/* conf */
conf_t   g_conf;
char *conf_file;

sig_atomic_t vb_reap;
sig_atomic_t vb_quit;
sig_atomic_t vb_worker_quit;
static void vb_signal_handler(int signo);

static vb_signal_t signals[] = {
    {SIGTERM, "SIGTERM", "", vb_signal_handler},
    {SIGQUIT, "SIGQUIT", "", vb_signal_handler},
    {SIGCHLD, "SIGCHLD", "", vb_signal_handler},
    {SIGPIPE, "SIGPIPE, SIG_IGN", "", SIG_IGN},
    {SIGINT, "SIGINT", "", SIG_IGN},
    {0, NULL, "", NULL}
};

static symbol_t syms[] = {
    /* symbol_name,         function pointer,           optional */
    {"handle_init",         (void **)&dll.handle_init,          1},
    {"handle_fini",         (void **)&dll.handle_fini,          1},
    {"handle_open",         (void **)&dll.handle_open,          1},
    {"handle_close",        (void **)&dll.handle_close,         1},
    {"handle_input",        (void **)&dll.handle_input,         0},
    {"handle_process",      (void **)&dll.handle_process,       0},
    {"handle_process_post", (void **)&dll.handle_process_post,  1},
    {NULL, NULL, 0}
};

static void init_vb_processes() {
    int i = 0;
    for (i = 0; i < VB_MAX_PROCESSES; ++i) {
        vb_processes[i].pid = -1;
    }
}

static void process_get_status(void) {
    int     status;
    int     i;
    pid_t   pid;
    int     one = 0;

    for ( ; ; ) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid == 0) {
            return;
        }

        if (pid == -1) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == ECHILD && one) {
                return;
            }
            return;
        }
        one = 1;

        for (i = 0; i < vb_last_process; ++i) {
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
            shmq_stop_wait();
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

#ifdef HAVE_BACKTRACE
static void sig_segv_handler(int sig, siginfo_t *info, void *secret) {
    void *trace[100];
    char **messages = NULL;
    int i, trace_size = 0;
    struct sigaction sa;

    FATAL_LOG("Crashed by signal: %d", sig);
    /* Generate the stack trace. */
    trace_size = backtrace(trace, 100);
    messages = backtrace_symbols(trace, trace_size);
    FATAL_LOG("--- STACK TRACE --- ");
    for (i = 0; i < trace_size; ++i) {
        FATAL_LOG("%s", messages[i]);
    }

    /* Make sure we exit with the right signal at the end. So for
     * instance the core will be dumped if enabled. */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER | SA_ONSTACK | SA_RESETHAND;
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, NULL);
    raise(sig);
}
#endif /* HAVE_BACKTRACE */

static int init_signals() {
    vb_signal_t *sig;
    struct sigaction sa;
#ifdef HAVE_BACKTRACE
    static char alt_stack[SIGSTKSZ];
    stack_t ss = {
        .ss_size = SIGSTKSZ,
        .ss_sp = alt_stack,
    };
    sigaltstack(&ss, NULL);
#endif /* HAVE_BACKTRACE */
    
    for (sig = signals; sig->signo != 0; ++sig) {
        memset(&sa, 0, sizeof(struct sigaction));
        sa.sa_handler = sig->handler;
        sigemptyset(&sa.sa_mask);
        if (sigaction(sig->signo, &sa, NULL) == -1) {
            return -1;
        }
    }

#ifdef HAVE_BACKTRACE
    sigemptyset(&sa.sa_mask);
    /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction
     * is used. Otherwise, sa_handler is used. */
    sa.sa_flags = SA_NODEFER | SA_ONSTACK | SA_RESETHAND | SA_SIGINFO;
    sa.sa_sigaction = sig_segv_handler;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
#endif /* HAVE_BACKTRACE */

    return 0;
}

static pid_t spawn_process(child_proc_t proc, void *data, 
        const char *name, int respawn) {
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
                /* Only to respawn process exited abnormally. */ 
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
        void *data, char *proc_name, int n, int type) {
    int i = 0;

    for (i = 0; i < n; ++i) {
        if (spawn_process(func, data, proc_name, type) < 0) {
            continue;
        }
    }
}

/*
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
*/

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
        FATAL_LOG("sigprocmask failed");
        exit(1);
    }

    /* Create the notifier between worker processes and conn process. */
    if (notifier_create() < 0) {
        FATAL_LOG("Create notifier between workers and conn failed");
        exit(1);
    }

    if (!(recv_queue = shmq_create(
                conf_get_int_value(&g_conf, "shmq_recv", 1 << 20)))) {
        FATAL_LOG("Create shared memory queue for receiving failed");
        exit(1);
    }

    if (!(send_queue = shmq_create(
                conf_get_int_value(&g_conf, "shmq_send", 1 << 20)))) {
        FATAL_LOG("Create shared memory queue for sending failed");
        exit(1);
    }

    create_processes(conn_process_cycle, (void *)&g_conf, 
            PROG_NAME":[conn]", 1, VB_PROCESS_RESPAWN);
    create_processes(worker_process_cycle, (void *)&g_conf, 
            PROG_NAME":[worker]",
            conf_get_int_value(&g_conf, "worker_num", 4),
            VB_PROCESS_RESPAWN);

    /* Don't close any fds. Because the master will spawn process on 
     * the fly once the chile aborted. */
    /*
    notifier_close_rd();
    notifier_close_wr();
    */

    sigemptyset(&set);
    for ( ; ; ) {
        sigsuspend(&set);

        if (vb_reap) {
            vb_reap = 0;
            live = reap_children();
        }

        if (!live && vb_quit) {
            if (dll.handle_fini) {
                dll.handle_fini(&g_conf, vb_process);
            }

            /* release relevant resources */
            shmq_free(recv_queue);
            shmq_free(send_queue);
            unload_so(&handle);
            unlink(pid_file);
            exit(0);
        }

        if (vb_quit) {
            if (kill(0, SIGTERM) < 0) {
                FATAL_LOG("Kill all children failed");
                exit(0);
            }
            continue;
        }
    }
}

static struct option const long_options[] = {
    {"config", required_argument, NULL, 'c'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
};

static void print_info() {
    printf("[%s]: A network server bench.\n"
        "Version: %s\n"
        "Copyright (c) flygoast, flygoast@126.com\n"
        "Compiled at %s %s\n", PROG_NAME, VERBEN_VERSION, 
            __DATE__, __TIME__);
}

static void usage(int status) {
    if (status != EXIT_SUCCESS) {
        fprintf(stderr, "Try `%s --help' for more information.\n",
            PROG_NAME);
    } else {
        printf("\
Usage:%10.10s [--config=<conf_file> | -c] [start|stop]\n\
%10.10s       [--version | -v]\n\
%10.10s       [--help | -h]\n", PROG_NAME, " ", " ");
    }
}

static void parse_options(int argc, char **argv) {
    int c;
    while ((c = getopt_long(argc, argv, "c:hv", 
                    long_options, NULL)) != -1) {
        switch (c) {
        case 'c':
            conf_file = optarg;
            break;
        case 'h':
            usage(EXIT_SUCCESS);
            exit(EXIT_SUCCESS);
        case 'v':
            print_info();
            exit(EXIT_SUCCESS);
        default:
            usage(EXIT_FAILURE);
            exit(1);
        }
    }

    if (optind + 1 == argc) {
        if (!strcasecmp(argv[optind], "stop")) {
            daemon_action = DAEMON_STOP;
        } else if (!strcasecmp(argv[optind], "start")) {
            daemon_action = DAEMON_START;
        } else {
            usage(EXIT_FAILURE); 
            exit(1);
        }
    } else if (optind == argc) {
        daemon_action = DAEMON_START;
    } else {
        usage(EXIT_FAILURE);
        exit(1);
    }
}

/*---------------------------- main ---------------------------*/
int main(int argc, char *argv[]) {
    char **saved_argv;
    char *so_name = NULL;
    pid_t pid;

    vb_process = VB_PROCESS_MASTER;

    parse_options(argc, argv);

    if (!conf_file) conf_file = "./verben.conf";
    if (conf_init(&g_conf, conf_file) != 0) {
        BOOT_FAILED("Load conf file [%s]", conf_file);
    }
    init_vb_processes();

    if (init_signals() < 0) {
        BOOT_FAILED("Initialize signal handlers");
    }

    if (log_init(conf_get_str_value(&g_conf, "log_dir", "/tmp"),
            conf_get_str_value(&g_conf, "log_name", PROG_NAME".log"),
            conf_get_int_value(&g_conf, "log_level", LOG_LEVEL_ALL),
            conf_get_int_value(&g_conf, "log_size", LOG_FILE_SIZE),
            conf_get_int_value(&g_conf, "log_num", LOG_FILE_NUM),
            conf_get_int_value(&g_conf, "log_multi", LOG_MULTI_NO)) < 0) {
        BOOT_FAILED("Initialize log file");
    }

    /* Make the process to be the leader process of the group */
    if (setpgrp() < 0) {
        BOOT_FAILED("Set self to be leader of the process group");
    }

    /* load .so file */
    so_name = conf_get_str_value(&g_conf, "so_file", NULL);
    if (load_so(&handle, syms, so_name) < 0) {
        BOOT_FAILED("load so file %s", so_name ? so_name : "(NULL)");
    }

    pid_file = conf_get_str_value(&g_conf, "pid_file", PID_FILE);
    pid = pid_file_running(pid_file);

    if (daemon_action == DAEMON_START) {
        daemonize(1, 1);
        rlimit_reset();

        if (pid == (pid_t)-1) {
            BOOT_FAILED("Checking running daemon:%s", strerror(errno));
        } else if (pid > 0) {
            BOOT_FAILED("The daemon have been running, pid=%lu", 
                    (unsigned int)pid);
        }
    
        if ((pid_file_create(pid_file)) != 0) {
            BOOT_FAILED("Create pid file failed: %s", strerror(errno));
        }
    } else if (daemon_action == DAEMON_STOP) {
        if (pid == (pid_t)-1) {
            BOOT_FAILED("Checking running daemon:%s", strerror(errno));
        } else if (pid == 0) {
            BOOT_FAILED("No verben daemon running now");
        }

        if (kill(pid, SIGQUIT) != 0) {
            fprintf(stderr, "kill %u failed\n", (unsigned int)pid);
            exit(1);
        }
        exit(0);
    } else {
        usage(EXIT_FAILURE);
        exit(0);
    }

    /* Invoke the hook in master. */
    if (dll.handle_init) {
        if (dll.handle_init(NULL, vb_process) != 0) {
            BOOT_FAILED("Invoke hook handle_init in master");
        }
    }

    saved_argv = daemon_argv_dup(argc, argv);
    if (!saved_argv) {
        BOOT_FAILED("Duplicate argv");
    }

    daemon_set_title("%s:[master]", PROG_NAME);
    master_process_cycle();
    exit(0);
}
