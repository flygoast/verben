#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "verben.h"
#include "worker.h"
#include "shmq.h"
#include "conn.h"
#include "dll.h"

void worker_process_cycle(const void *data) {
    shm_msg *msg = NULL;
    shm_msg *temp_msg;
    int     msg_len;
    int     ret;
    char    *retdata;
    int     retlen;

    vb_process = VB_PROCESS_WORKER;
    
    if (dll.handle_init) {
        dll.handle_init("Worker init", vb_process);
    }

    for ( ; ; ) {
        if (vb_worker_quit) {
            if (dll.handle_fini) {
                dll.handle_fini("worker_finish", vb_process);
            }
            printf("process[%d] will exit...\n", getpid());
            exit(0);
        }

        if (shmq_pop(recv_queue, (void **)&msg, &msg_len, 
                    SHMQ_WAIT|SHMQ_LOCK) != 0) {
            if (msg) free(msg);
            continue;
        }

        ret = dll.handle_process((char*)msg + sizeof(shm_msg),
                msg_len - sizeof(shm_msg),
                &retdata, &retlen, &msg->sk);
        temp_msg = (shm_msg*)realloc(msg, sizeof(shm_msg) + retlen);
        assert(temp_msg);
        memcpy((char *)temp_msg + sizeof(shm_msg), retdata, retlen);

        if (shmq_push(send_queue, temp_msg, 
                    sizeof(shm_msg) + retlen, 
                    SHMQ_WAIT | SHMQ_LOCK) != 0) {
            continue;
        }
        notifier_write();

        free(temp_msg);
    }
}
