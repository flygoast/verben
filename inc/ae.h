#ifndef __AE_H_INCLUDED__
#define __AE_H_INCLUDED__

#define AE_SETSIZE  (1024 * 10)     /* Max number of fd supported */

#define AE_OK   0
#define AE_ERR  -1

#define AE_NONE         0
#define AE_READABLE     1
#define AE_WRITABLE     2

#define AE_FILE_EVENTS  1
#define AE_TIME_EVENTS  2
#define AE_ALL_EVENTS   (AE_FILE_EVENTS | AE_TIME_EVENTS)
#define AE_DONT_WAIT    4

#define AE_NOMORE       -1

/* Macros */
#define AE_NOTUSED(V)   ((void)V)

struct ae_event_loop;

/* Type and data structures */
typedef void ae_file_proc(struct ae_event_loop *el, int fd, 
        void *client_data, int mask);
typedef int ae_time_proc(struct ae_event_loop *el, long long id,
        void *client_data);
typedef void ae_event_finalizer_proc(struct ae_event_loop *el,
        void *client_data);
typedef void ae_before_sleep_proc(struct ae_event_loop *el);

/* File event structure */
typedef struct ae_file_event {
    int mask; /* one of AE_(READABLE|WRITABLE) */
    ae_file_proc    *r_file_proc;
    ae_file_proc    *w_file_proc;
    void *client_data;
} ae_file_event;

/* Time event structure */
typedef struct ae_time_event {
    long long id;   /* Time event identifier. */
    long when_sec;  /* seconds */
    long when_ms;   /* milliseconds */
    ae_time_proc    *time_proc;
    ae_event_finalizer_proc *finalizer_proc;
    void *client_data;
    struct ae_time_event *next;
} ae_time_event;

/* A fired event */
typedef struct ae_fired_event {
    int fd;
    int mask;
} ae_fired_event;

/* State of an event base program */
typedef struct ae_event_loop {
    int maxfd;
    long long time_event_next_id;
    ae_file_event   events[AE_SETSIZE]; /* Registered events */
    ae_fired_event  fired[AE_SETSIZE];  /* Fired events */
    ae_time_event   *time_event_head;
    int stop;
    void *api_data; /* This is used for polling API specific data. */
    ae_before_sleep_proc    *before_sleep;
} ae_event_loop;

/* Prototypes */
ae_event_loop   *ae_create_event_loop(void);
void ae_free_event_loop(ae_event_loop *el);
void ae_stop(ae_event_loop *el);
int ae_create_file_event(ae_event_loop *el, int fd, int mask,
        ae_file_proc *proc, void *client_data);
void ae_delete_file_event(ae_event_loop *el, int fd, int mask);
int ae_get_file_event(ae_event_loop *el, int fd);
long long ae_create_time_event(ae_event_loop *el, long long milliseconds,
        ae_time_proc *proc, void *client_data,
        ae_event_finalizer_proc *finalizer_proc);
int ae_delete_time_event(ae_event_loop *el, long long id);
int ae_process_events(ae_event_loop *el, int flags);
int ae_wait(int fd, int mask, long long milliseconds);
void ae_main(ae_event_loop *el);
char *ae_get_api_name(void);
void ae_set_before_sleep_proc(ae_event_loop *el, 
        ae_before_sleep_proc *before_sleep);

#endif /*__AE_H_INCLUDED__ */
