#ifndef __SHMQ_H_INCLUDED__
#define __SHMQ_H_INCLUDED__

/* flags for push/pop */
#define	SHMQ_WAIT   0x01
#define SHMQ_LOCK   0x02

typedef struct shm_queue shmq_t;

extern shmq_t *shmq_create(int length);
extern int  shmq_init(shmq_t *q, int length);
extern void shmq_destroy(shmq_t *q);
extern void shmq_free(shmq_t *q);
extern int shmq_push(shmq_t *q, void *data, int len, int flag);
extern int shmq_pop(shmq_t *q, void **retdata, int *len, int flag);

#endif /* __SHMQ_H_INCLUDED__ */
