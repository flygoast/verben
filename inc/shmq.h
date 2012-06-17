/* Shared memory looped queue */
#ifndef __SHMQ_H_INCLUDED__
#define __SHMQ_H_INCLUDED__

#define SHMQ_WAIT       0x01
#define SHMQ_LOCK       0x02

typedef struct shm_queue shmq_t;

extern void shmq_stop_wait();
extern shmq_t *shmq_create(size_t sz);
extern int shmq_init(shmq_t *q, size_t sz);
extern void shmq_destroy(shmq_t *q);
extern void shmq_free(shmq_t *q);
extern int shmq_push(shmq_t *q, void *data, size_t len, int flags);
extern int shmq_pop(shmq_t *q, void **retdata, int *len, int flags);

#endif /* __SHMQ_H_INCLUDED__ */
