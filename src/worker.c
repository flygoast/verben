#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include "verben.h"
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
            ERROR_LOG("Invoke hook handle_init in worker[%d] failed",
                    getpid());
            exit(0);
        }
    }

    for ( ; ; ) {
        if (vb_worker_quit) {
            if (dll.handle_fini) {
                dll.handle_fini(data, vb_process);
            }
            DEBUG_LOG("Process[%d] will exit", getpid());
            exit(0);
        }

        if (shmq_pop(recv_queue, (void **)&msg, &msg_len, 
                    SHMQ_WAIT|SHMQ_LOCK) != 0) {
            ERROR_LOG("shmq_pop from recv_queue in worker[%d] failed",
                    getpid());
            if (msg) free(msg);
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

        if (shmq_push(send_queue, temp_msg, 
                    sizeof(shm_msg) + retlen, 
                    SHMQ_WAIT | SHMQ_LOCK) != 0) {
            ERROR_LOG("shmq_push to send_queue in worker[%d] failed",
                    getpid());
            continue;
        }

        if (notifier_write() < 0) {
            ERROR_LOG("notifier_write failed:%s", strerror(errno));
            continue;
        }

        free(temp_msg);
    }
}
