#ifndef __DLIST_H_INCLUDED__
#define __DLIST_H_INCLUDED__

typedef struct dlist_node {
    struct dlist_node *prev;
    struct dlist_node *next;
    void *value;
} dlist_node;

typedef struct dlist_iter {
    dlist_node *next;
    int direction;
} dlist_iter;

typedef struct dlist {
    dlist_node *head;
    dlist_node *tail;
    void *(*dup)(void *ptr);
    void (*free)(void *ptr);
    int (*match)(void *ptr, void *key);
    unsigned int len;
} dlist;

/* Functions implemented as macros */
#define dlist_length(l)     ((l)->len)
#define dlist_first(l)      ((l)->head) 
#define dlist_last(l)       ((l)->tail)
#define dlist_prev_node(n)  ((n)->prev)
#define dlist_next_node(n)  ((n)->next)
#define dlist_node_value(n) ((n)->value)

#define dlist_set_dup(l,m)      ((l)->dup = (m))
#define dlist_set_free(l,m)     ((l)->free = (m))
#define dlist_set_match(l,m)    ((l)->match = (m)) 

#define dlist_get_dup(l)    ((l)->dup)
#define dlist_get_free(l)   ((l)->free)
#define dlist_get_match(l)  ((l)->match)

/* Prototypes */
dlist *dlist_init(void);
void dlist_destroy(dlist *dl);
dlist *dlist_add_node_head(dlist *dl, void *value);
dlist *dlist_add_node_tail(dlist *dl, void *value);
dlist *dlist_insert_node(dlist *dl, dlist_node *ponode, 
        void *value, int after);
void dlist_delete_node(dlist *dl, dlist_node *pnode);
dlist_iter *dlist_get_iterator(dlist *dl, int direction);
dlist_node *dlist_next(dlist_iter *iter);
void dlist_destroy_iterator(dlist_iter *iter);
dlist *dlist_dup(dlist *orig);
dlist_node *dlist_search_key(dlist *dl, void *key);
dlist_node *dlist_index(dlist *dl, int index);

void dlist_rewind(dlist *dl, dlist_iter *iter);
void dlist_rewind_tail(dlist *dl, dlist_iter *iter);

/* Directions for iterators */
#define DLIST_START_HEAD    0
#define DLIST_START_TAIL    1

#endif /* __DLIST_H_INCLUDED__ */
