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
#include "plugin.h"

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
            ERROR_LOG("shmq_pop from recv_queue in worker[%d] failed:%s",
                    getpid(), strerror(errno));
            if (msg) free(msg);
            continue;
        } else if (ret == 1) {
            if (msg) free(msg);
            continue;
        }

        retdata = NULL;

        ret = dll.handle_process((char*)msg + sizeof(shm_msg),
                msg_len - sizeof(shm_msg),
                &retdata, &retlen, msg->remote_ip, msg->remote_port);

        assert(retlen >= 0);
        /* Worker processes don't modify the message header segment. */
        if (retlen > 0) {
            temp_msg = (shm_msg*)realloc(msg, sizeof(shm_msg) + retlen);
        } else {
            temp_msg = msg;
        }

        if (!temp_msg) {
            FATAL_LOG("Out of memory");
            exit(1);
        }

        /* Whether close the connection after send the response. */
        if (ret & (VERBEN_CONN_CLOSE | VERBEN_ERROR)) {
            temp_msg->close_conn = 1;
        } else {
            temp_msg->close_conn = 0;
        }

        if (!(ret & VERBEN_ERROR)) {
            if (retdata && retlen > 0) {
                memcpy((char *)temp_msg + sizeof(shm_msg), retdata, retlen);
            }
        }

        if (dll.handle_process_post) {
            dll.handle_process_post(retdata, retlen);
        }

        ret = shmq_push(send_queue, temp_msg, sizeof(shm_msg) + retlen, 
                    SHMQ_WAIT | SHMQ_LOCK);

        free(temp_msg);

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
    }
}
