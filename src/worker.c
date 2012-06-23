#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include "verben.h"
#include "daemon.h"
#include "worker.h"
#include "shmq.h"
#include "conn.h"
#include "dll.h"
#include "conf.h"
#include "log.h"
#include "notifier.h"

void worker_process_cycle(void *data) {
    shm_msg *msg = NULL;
    shm_msg *temp_msg;
    int     msg_len;
    int     ret;
    char    *retdata;
    int     retlen;

    vb_process = VB_PROCESS_WORKER;
    
    if (dll.handle_init) {
        if (dll.handle_init(data, vb_process) < 0) {
            boot_notify(-1, "Invoke hook handle_init in worker[%d]",
                    getpid());
            kill(getppid(), SIGQUIT);
            exit(0);
        }
    }
    redirect_std();

    for ( ; ; ) {
        if (vb_worker_quit) {
            if (dll.handle_fini) {
                dll.handle_fini(data, vb_process);
            }
            exit(0);
        }

        ret = shmq_pop(recv_queue, (void **)&msg, &msg_len, 
                    SHMQ_WAIT|SHMQ_LOCK);
        if (ret < 0) {
            ERROR_LOG("shmq_pop from recv_queue in worker[%d] failed",
                    getpid());
            if (msg) free(msg);
            continue;
        } else if (ret == 1) {
            continue;
        }

        ret = dll.handle_process((char*)msg + sizeof(shm_msg),
                msg_len - sizeof(shm_msg),
                &retdata, &retlen, msg->remote_ip, msg->remote_port);
        temp_msg = (shm_msg*)realloc(msg, sizeof(shm_msg) + retlen);
        if (!temp_msg) {
            FATAL_LOG("Out of memory");
            exit(0);
        }
        memcpy((char *)temp_msg + sizeof(shm_msg), retdata, retlen);

        ret = shmq_push(send_queue, temp_msg, sizeof(shm_msg) + retlen, 
                    SHMQ_WAIT | SHMQ_LOCK);
        if (ret < 0) {
            ERROR_LOG("shmq_push to send_queue in worker[%d] failed",
                    getpid());
            continue;
        } else if (ret == 1) {
            continue;
        }

        if (notifier_write() < 0) {
            ERROR_LOG("notifier_write failed:%s", strerror(errno));
            continue;
        }

        free(temp_msg);
    }
}
