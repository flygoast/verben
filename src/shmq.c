#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include "atomic.h"
#define PTHREAD_LOCK_MODE
#include "lock.h"
#include "shmq.h"

#define CYCLE_WAIT_NANO_SEC 200000 /* 0.2 minisecond */
#define SHMQ_BLK(q, off)    (shmq_block_t*)((char*)q->addr+(off))

/* shmq block type */
#if __WORDSIZE == 32 /* 32 bit machine */
#define PAD_BLOCK       0x80000000
#define MAX_BLK_SIZE    0x7FFFFFFF
#elif __WORDSIZE == 64  /* 64 bit machine */
#define PAD_BLOCK       0x8000000000000000
#define MAX_BLK_SIZE    0x7FFFFFFFFFFFFFFF
#else
#error "Invalid word size."
#endif /* __WORDSIZE */

#define OPT_LOCK(l, flag)  do { \
    if ((l) && (flag & SHMQ_LOCK)) { \
        LOCK_LOCK(l); \
    } \
} while (0)

#define OPT_UNLOCK(l, flag) do { \
    if ((l) && (flag & SHMQ_LOCK)) { \
        LOCK_UNLOCK(l); \
    } \
} while (0)

#define SHMQ_ALIGN(n) \
    (((n)+(sizeof(struct shmq_block)-1))&~(sizeof(struct shmq_block)-1))

typedef struct shmq_header {
    volatile off_t  head;   /* offset of queue head to the addr of shm */
    volatile off_t  tail;   /* offset of queue tail to the addr of shm */
    atomic_t        blk_cnt;    /* block count in the queue. */
    lock_t          lock;
} shmq_header_t;

typedef struct shmq_block {
    uintptr_t   size;       /* the lowest bit used to indicate type */
    char        data[0];    /* stub for transmited data */
} shmq_block_t;

struct shm_queue {
    shmq_header_t   *addr;
    off_t           start;
    size_t          size;
};

static int shmq_stop = 0;

static int shmq_header_init(shmq_t *q) {
    LOCK_INIT(&q->addr->lock);
    q->addr->head = q->start;
    q->addr->tail = q->start;
    atomic_set(&(q->addr->blk_cnt), 0);
    return 0;
}

void shmq_stop_wait() {
    shmq_stop = 1;
}

/* Initialize a shared memory queue indicated by 'q' */
int shmq_init(shmq_t *q, size_t sz) {
    assert(q && (sz > 0));
    sz = SHMQ_ALIGN(sz);
    q->addr = (shmq_header_t*)mmap(NULL, sz, PROT_READ|PROT_WRITE,
            MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if (q->addr == MAP_FAILED) {
        return -1;
    }
    q->size = sz;
    q->start = SHMQ_ALIGN(sizeof(shmq_header_t));
    return shmq_header_init(q);
}

/* Allocate a shared memory queue, initialize and return it. */
shmq_t *shmq_create(size_t sz) {
    shmq_t *q = (shmq_t*)malloc(sizeof(*q));
    assert(q);

    if (shmq_init(q, sz) != 0) {
        free(q);
        return NULL;
    }
    return q;
}

void shmq_destroy(shmq_t *q) {
    assert(q);
    LOCK_DESTROY(&q->addr->lock);
    if (q->addr != MAP_FAILED) {
        munmap(q->addr, q->size);
        q->addr = MAP_FAILED;
        q->size = 0;
    }
}

void shmq_free(shmq_t *q) {
    assert(q);
    shmq_destroy(q);
    free(q);
}

int shmq_push(shmq_t *q, void *data, size_t len, int flag) {
    off_t head, tail;
    struct timespec ts;
    shmq_block_t *blk;
    int req_size = SHMQ_ALIGN(sizeof(shmq_block_t) + len);
    ts.tv_sec = 0;
    ts.tv_nsec = CYCLE_WAIT_NANO_SEC;

    OPT_LOCK(&q->addr->lock, flag);    

shmq_push_again:
    if (shmq_stop) {
        goto shmq_push_error;
    }

    head = q->addr->head;
    tail = q->addr->tail;

    if (tail == q->size) {
        tail = q->start;
        q->addr->tail = q->start;
    }

    if (tail >= head) {
        if (tail + req_size < q->size ||
                (head != q->start && tail + req_size == q->size)) {
            /* Not to make tail and head to be equal. It will
             * conflict with the empty situation. */
            goto shmq_push_success;
        }

        if (q->start + req_size >= head) {
            /* No space to hold this block. */
            if (flag & SHMQ_WAIT) {
                nanosleep(&ts, NULL);
                goto shmq_push_again;
            }
            goto shmq_push_error;
        }

        /* add a pad block */
        blk = SHMQ_BLK(q, tail);
        blk->size = (q->size - q->addr->tail) | PAD_BLOCK;
        q->addr->tail = q->start;
        goto shmq_push_success;
    } 

    if (tail < head) {
        if (tail + req_size < head) {
            /* can hold the block */
            goto shmq_push_success;
        }

        /* no space to hold the block */
        if (flag & SHMQ_WAIT) {
            nanosleep(&ts, NULL);
            goto shmq_push_again;
        }
        goto shmq_push_error;
    }

shmq_push_success:
    blk = SHMQ_BLK(q, q->addr->tail);
    blk->size = len + sizeof(shmq_block_t);
    memcpy(blk->data, data, len);
    atomic_inc(&q->addr->blk_cnt);
    q->addr->tail += req_size;
    OPT_UNLOCK(&q->addr->lock, flag);
    return 0;

shmq_push_error:
    OPT_UNLOCK(&q->addr->lock, flag);
    return -1;
}


/* The caller should free the memory returned by 'retdata'. */
int shmq_pop(shmq_t *q, void **retdata, int *len, int flag) {
    off_t head, tail;
    shmq_block_t *blk;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = CYCLE_WAIT_NANO_SEC;

    assert(retdata && len);
    *retdata = NULL;
    
    OPT_LOCK(&q->addr->lock, flag);

shmq_pop_again:
    if (shmq_stop) {
        goto shmq_pop_error;
    }

    tail = q->addr->tail;
    head = q->addr->head;
    if (head == q->size) {
        head = q->start;
        q->addr->head = q->start;
    }

    if (head == tail) {
        if (flag & SHMQ_WAIT) {
            nanosleep(&ts, NULL);
            goto shmq_pop_again;
        }
        goto shmq_pop_error;
    }

    if (head > tail) {
        blk = SHMQ_BLK(q, head);
        if (blk->size & PAD_BLOCK) {
            q->addr->head = q->start;
            goto shmq_pop_again;
        }
        goto shmq_pop_success;
    }
    
    if (head < tail) {
        blk = SHMQ_BLK(q, head);
        if (blk->size & PAD_BLOCK) {
            q->addr->head = q->start;
            goto shmq_pop_again;
        }
        goto shmq_pop_success;
    }

shmq_pop_success:
    *len = blk->size - sizeof(shmq_block_t);
    *retdata = malloc(*len);
    if (*retdata == NULL) {
        goto shmq_pop_error;
    }
    memcpy(*retdata, blk->data, *len);
    atomic_dec(&q->addr->blk_cnt);
    q->addr->head += SHMQ_ALIGN(blk->size & MAX_BLK_SIZE);
    OPT_UNLOCK(&q->addr->lock, flag);
    return 0;

shmq_pop_error:
    OPT_UNLOCK(&q->addr->lock, flag);
    return -1;
}

/* gcc shmq.c lock.c -DSHMQ_TEST_MAIN -I../inc -lpthread -g */
#ifdef SHMQ_TEST_MAIN
#include <stdio.h>
#include <unistd.h>

void shmq_dump(shmq_t *q) {
    off_t off;
    shmq_block_t *blk;

    if (atomic_read(&q->addr->blk_cnt) == 0) {
        printf("[]\n");
        return;
    }
    
    printf("*");
    off = q->addr->head;
    while (1) {
        blk = (shmq_block_t *)((char*)q->addr + off);
        if (blk->size & PAD_BLOCK) {
            off += SHMQ_ALIGN(blk->size & MAX_BLK_SIZE) ;
            printf("[PAD]");
        } else {
            printf("[%s]", blk->data);
            off += SHMQ_ALIGN(blk->size);
        }

        if (off == q->size) {
            off = q->start;
        }

        if (off == q->addr->tail) {
            break;
        }
    }
    printf("*\n");
}

typedef struct shm_msg {
    void        *cli;
    char        remote_ip[16];
    int         remote_port;
    char        data[0];
} __attribute__((packed)) shm_msg;

int main(int argc, char *argv[]) {
    int i = 0;
    shm_msg *result;
    int len;
    shmq_t *q;
    pid_t pid;
    char *test = "Hello\r\n";

    q = shmq_create(1048576);
    shm_msg *msg = (shm_msg *)malloc(sizeof(*msg) + 7);
    memcpy(msg->data, test, 7);

    while (i++ < 5) {
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork failed:%s\n", strerror(errno));
            exit(1);
        } else if (pid == 0) {
            printf("PID:%d, PPID:%d\n", getpid(), getppid());
            while (1) {
                if (shmq_pop(q, (void*)&result, &len, SHMQ_WAIT | SHMQ_LOCK) < 0) {
                    fprintf(stderr, "shmq_pop failed\n");
                    exit(1);
                }
                result->data[6] = '\0';
                printf("[%d]:%s\n", getpid(), result->data);
                free(result);
            }
            printf("something wrong\n");
            exit(0);
        }
    }
    
    while (1) {
        if (shmq_push(q, msg, sizeof(*msg) + 7, SHMQ_WAIT) < 0) {
            fprintf(stderr, "shmq_push failed\n");
            printf("start:%ld, head:%d, tail:%d\n", q->start,
                    q->addr->head,
                    q->addr->tail);
            exit(1);
        }
    }
    exit(0);
}

#endif /* SHMQ_TEST_MAIN */
