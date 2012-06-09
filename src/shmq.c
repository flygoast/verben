#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include "atomic.h"
#define PTHREAD_LOCK_MODE   /* for lock_t */
#include "lock.h"
#include "shmq.h"

#define CYCLE_WAIT_NANO_SEC	200000 /* 0.2 second */
#define SHMQ_HEAD_BLK(q)    (shmq_block_t*)((char*)q->addr+q->addr->head)
#define SHMQ_TAIL_BLK(q)    (shmq_block_t*)((char*)q->addr+q->addr->tail)

/* shmq block type */
#define DAT_BLOCK   0
#define PAD_BLOCK   1

#define SEM_LOCK(l,flag)    do { \
    if ((l) && (flag & SHMQ_LOCK)) { \
        LOCK_LOCK(l); \
    } \
} while (0)

#define SEM_UNLOCK(l,flag)  do { \
    if ((l) && (flag & SHMQ_LOCK)) { \
        LOCK_UNLOCK(l); \
    } \
} while (0)


/* volatile to avoid cache of head and tail pointer */
typedef struct shmq_header {
    volatile int head;  /* offset of queue head to the addr of shm */
    volatile int tail;  /* offset of queue tail to the addr of shm */
    atomic_t blk_cnt;   /* block count in the queue */
} __attribute__((packed)) shmq_header_t;

typedef struct shmq_block {
    unsigned int    length;     /* the total length of the block */
    char            type;
    char            data[0];
} __attribute__((packed)) shmq_block_t;

struct shm_queue {
    shmq_header_t   *addr;
    unsigned int    length;
    lock_t          lock;   /* A lock for asynchronous */
};

/* Initialize a shared memory queue passed by 'q'. */
int shmq_init(shmq_t *q, int length) {
    LOCK_INIT(&q->lock);

    assert(q && length > 0);
    q->length = length;
    q->addr = (shmq_header_t*)mmap(NULL, length, PROT_READ|PROT_WRITE,
        MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if (q->addr == MAP_FAILED) {
        return -1;
    }
    q->addr->head = sizeof(shmq_header_t);
    q->addr->tail = sizeof(shmq_header_t);
    atomic_set(&(q->addr->blk_cnt), 0);
    return 0;
}

/* Allocate a shared memory queue, initialize and return it. */
shmq_t *shmq_create(int length) {
    shmq_t *q = (shmq_t*)malloc(sizeof(*q));
    if (!q) {
        return NULL;
    }

    if (shmq_init(q, length) != 0) {
        free(q);
        return NULL;
    }
    return q;
}

void shmq_destroy(shmq_t *q) {
    assert(q);
    if (q->addr != NULL) {
        munmap(q->addr, q->length);
        q->addr = NULL;
    }

    LOCK_DESTROY(&q->lock);
}

void shmq_free(shmq_t *q) {
    assert(q);
    shmq_destroy(q);
    free(q);
}

/* Adjust the head position to hold entire the block.
   When adjust succeeded or no need to adjust, 0  was
   returned, -1 was returned when the queue is full. */
static int shmq_adjust_head(shmq_t *q, int length) {
    int tail = q->addr->tail;
    int head = q->addr->head;
    shmq_block_t *padding;
    int surplus = q->length - head;

    /* queue have not adequate space at the bottom of 
       the memory buffer to hold the block. */
    if (surplus < length) {
        if (tail == sizeof(shmq_header_t) || tail > head) {
            /* The queue is full. */
            return -1;
        } else if (surplus < sizeof(shmq_block_t)) {
            /* Have not adequate space to add a padding.
               Move head to the top of the buffer. */
            q->addr->head = sizeof(shmq_header_t);
        } else {
            /* Add a padding */
            padding = SHMQ_HEAD_BLK(q);
            padding->type = PAD_BLOCK;
            padding->length = surplus;
            q->addr->head = sizeof(shmq_header_t);
        }
    }
    return 0;
}

static void shmq_adjust_tail(shmq_t *q) {
    shmq_block_t *padding;

    if (q->addr->head >= q->addr->tail) {
        /* No need to adjust tail position. */
        return;
    }

    padding = SHMQ_TAIL_BLK(q);
    if ((q->length - q->addr->tail < sizeof(shmq_block_t)) ||
            padding->type == PAD_BLOCK) {
        q->addr->tail = sizeof(shmq_header_t);
    }
    return;
}

/* Enqueue at head end, and dequeue at tail end. */
static int shmq_push_wait(shmq_t *q, int length, int flag) {
    struct timespec tv = {0, CYCLE_WAIT_NANO_SEC};

    /* Adjust the head position to the position from where
       the queue can contain the entire block(containing
       the data). */
    while (shmq_adjust_head(q, length) < 0) {
        if (flag & SHMQ_WAIT) {
            nanosleep(&tv, NULL);
        } else {
            return -1;
        }
    }

push_wait_again:
    while (q->addr->tail > q->addr->head && 
            q->addr->tail < q->addr->head + length + 1) {
        if (flag & SHMQ_WAIT) {
            nanosleep(&tv, NULL);
        } else {
            return -1;
        }
    }

    /* Adjust the head position again. */
    while (shmq_adjust_head(q, length) < 0) {
        if (flag & SHMQ_WAIT) {
            nanosleep(&tv, NULL);
        } else {
            return -1;
        }
    }

    if (q->addr->tail > q->addr->head && 
            q->addr->tail < q->addr->head + length + 1) {
        goto push_wait_again;
    }

    return 0;
}

static int shmq_pop_wait(shmq_t *q, int flag) {
    struct timespec tv = {0, CYCLE_WAIT_NANO_SEC};

    /* Adjust tail position. */
    shmq_adjust_tail(q);

pop_wait_again:
    while (q->addr->tail == q->addr->head) {
        if ((flag & SHMQ_WAIT)) {
            nanosleep(&tv, NULL);
        } else {
            return -1;
        }
    }

    shmq_adjust_tail(q);
    if (q->addr->tail == q->addr->head) {
        goto pop_wait_again;
    }
    return 0;
}

int shmq_push(shmq_t *q, void *data, int len, int flag) {
    int ret = -1;
    int real_len = sizeof(shmq_block_t) + len;

    SEM_LOCK(&q->lock, flag);

    if (shmq_push_wait(q, real_len, flag) == 0) {
        shmq_block_t *next_blk = SHMQ_HEAD_BLK(q);
        next_blk->type = DAT_BLOCK;
        next_blk->length = real_len;
        memcpy((void*)((char*)next_blk + sizeof(shmq_block_t)),
            data, len);
        q->addr->head += real_len;
        atomic_inc(&q->addr->blk_cnt);
        ret = 0;
    }

    SEM_UNLOCK(&q->lock, flag);
    return ret;
}

/* The caller should to free the memory returned by 'retdata'. */
int shmq_pop(shmq_t *q, void **retdata, int *len, int flag) {
    shmq_block_t *cur_blk;
    int ret = -1;

    SEM_LOCK(&q->lock, flag);

    if (shmq_pop_wait(q, flag) == 0) {
        cur_blk = SHMQ_TAIL_BLK(q);
        *len = cur_blk->length - sizeof(shmq_block_t);
        *retdata = malloc(*len);
        if (!*retdata) {
            goto shmq_pop_end;
        }
        memcpy(*retdata, ((char*)cur_blk + sizeof(shmq_block_t)), *len);
        atomic_dec(&q->addr->blk_cnt);
        q->addr->tail += cur_blk->length;
        ret = 0;
    }
shmq_pop_end:
    SEM_UNLOCK(&q->lock, flag);
    return ret;
}

#ifdef SHMQ_TEST_MAIN
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int i = 0;
    void *result;
    int len;
    shmq_t *q;
    pid_t pid;
    char *test = "Hello world";
    struct test_struct {
        char    name[8];
        int     id;
    } st;
    memcpy(st.name, "A TEST", 7);
    st.id = 0xFFFFFFFF;

    q = shmq_create(1 << 20);
    assert(q);
    assert(shmq_push(q, test, strlen(test) + 1, SHMQ_WAIT) == 0);
    assert(shmq_push(q, &st, sizeof(st), SHMQ_WAIT) == 0);
    assert(shmq_push(q, test, strlen(test) + 1, SHMQ_WAIT) == 0);
    assert(shmq_push(q, test, strlen(test) + 1, SHMQ_WAIT) == 0);
    assert(shmq_pop(q, &result, &len, SHMQ_WAIT) == 0);
    printf("%s\n", (char *)result);
    free(result);
    assert(shmq_pop(q, &result, &len, SHMQ_WAIT) == 0);
    printf("%s:0x%X\n", ((struct test_struct*)result)->name,
            ((struct test_struct*)result)->id);
    free(result);
    assert(shmq_pop(q, &result, &len, SHMQ_WAIT) == 0);
    printf("%s\n", (char*)result);
    free(result);
    assert(shmq_pop(q, &result, &len, SHMQ_WAIT) == 0);
    printf("%s\n", (char *)result);
    free(result);
    shmq_free(q);

    printf("shmq_header_size:%d, sizeof(st):%d, block size:%d\n", 
        sizeof(shmq_header_t), sizeof(st), sizeof(shmq_block_t));
    q = shmq_create(110);
    assert(shmq_push(q, &st, sizeof(st), SHMQ_WAIT) == 0);
    assert(shmq_push(q, &st, sizeof(st), SHMQ_WAIT) == 0);
    assert(shmq_push(q, &st, sizeof(st), SHMQ_WAIT) == 0);
    /* assert(shmq_push(q, &st, sizeof(st), SHMQ_WAIT) == -1); */
    shmq_free(q);

    q = shmq_create(1 << 20);
    while (i++ < 5) {
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork failed:%s\n", strerror(errno));
            exit(1);
        } else if (pid == 0) {
            while (1) {
                if (shmq_pop(q, &result, &len, SHMQ_WAIT | SHMQ_LOCK) < 0) {
                    fprintf(stderr, "shmq_pop failed\n");
                    exit(1);
                }
                printf("[%d]:%s:0x%X\n", getpid(), 
                    ((struct test_struct*)result)->name,
                    ((struct test_struct*)result)->id);
                free(result);
            }
            exit(0);
        }
    }
    
    while (1) {
        if (shmq_push(q, &st, sizeof(st), SHMQ_WAIT) < 0) {
            fprintf(stderr, "shmq_push failed\n");
            exit(1);
        }
    }
    exit(0);
}

#endif /* SHMQ_TEST_MAIN */
