/* It's a generic doubly linked list implementation. */
#include <stdlib.h>
#include "dlist.h"

/* Initialize a new list. The initalized list should be freed with
   dlist_destroy(), but private value of each node need to be freed
   by the user before to call dlist_destroy if list's 'free' pointer
   is NULL. Upon error, NULL is returned. Otherwise the pointer to 
   the new doubly linked list. */
dlist *dlist_init(void) {
    struct dlist *dl;

    if ((dl = malloc(sizeof(*dl))) == NULL) {
        return NULL;
    }

    dl->head = NULL;
    dl->tail = NULL;
    dl->len = 0;
    dl->dup = NULL;
    dl->free = NULL;
    dl->match = NULL;
    return dl;
}

/* Destroy the whole list. If the list's 'free' pointer is not 
   NULL, the value will be freed automately first. */
void dlist_destroy(dlist *dl) {
    unsigned int len;
    dlist_node *curr = NULL;
    dlist_node *next = NULL;

    curr = dl->head;
    len = dl->len;
    while (len--) {
        next = curr->next;
        if (dl->free)
            dl->free(curr->value);
        free(curr);
        curr = next;
    }
    free(dl);
}

/* Add a new node to the list's head, containing the specified 'value'
   pointer as value. On error, NULL is returned and no operation is 
   performed (i.e. the list remains unaltered). On success the 'dl'
   pointer you pass to the function is returned. */
dlist *dlist_add_node_head(dlist *dl, void *value) {
    dlist_node *node = NULL;
    if ((node = malloc(sizeof(*node))) == NULL) {
        return NULL;
    }
    node->value = value;
    if (dl->len == 0) {
        dl->head = node;
        dl->tail = node;
        node->prev = NULL;
        node->next = NULL;
    } else {
        node->prev = NULL;
        node->next = dl->head;
        dl->head->prev = node;
        dl->head = node;
    }
    dl->len++;
    return dl;
}
        
/* Add a new node to the list's tail, containing the specified 'value'
   pointer as value. On error, NULL is returned and no operation is 
   performed (i.e. the list remains unaltered). On success the 'dl'
   pointer you pass to the function is returned. */
dlist *dlist_add_node_tail(dlist *dl, void *value) {
    dlist_node *node;
    if ((node = malloc(sizeof(*node))) == NULL) {
        return NULL;
    }

    node->value = value;
    if (dl->len == 0) {
        dl->head = node;
        dl->tail = node;
        node->prev = NULL;
        node->next = NULL;
    } else {
        node->prev = dl->tail;
        node->next = NULL;
        dl->tail->next = node;
        dl->tail = node;
    }
    dl->len++;
    return dl;
}

dlist *dlist_insert_node(dlist *dl, dlist_node *old_node, 
        void *value, int after) {
    dlist_node *node;
    if ((node = malloc(sizeof(*node))) == NULL)
        return NULL;

    node->value = value;
    if (after) {
        node->prev = old_node;
        node->next = old_node->next;
        if (dl->tail == old_node) {
            dl->tail = node;
        }
    } else {
        node->next = old_node;
        node->prev = old_node->prev;
        if (dl->head == old_node) {
            dl->head = node;
        }
    }

    if (node->prev != NULL) {
        node->prev->next = node;
    }

    if (node->next != NULL) {
        node->next->prev = node;
    }
    dl->len++;
    return dl;
}

/* Remove the specified node from the specified list. If the 
   list's 'free' pointer is not NULL, the value will be freed 
   automately first. */
void dlist_delete_node(dlist *dl, dlist_node *node) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        dl->head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        dl->tail = node->prev;
    }

    if (dl->free) {
        dl->free(node->value);
    }

    free(node);
    dl->len--;
}

/* Return a list iterator. After the initialization each call to 
   dlist_next will return the next element of the list. */
dlist_iter *dlist_get_iterator(dlist *dl, int direction) {
    dlist_iter *iter;
    if ((iter = malloc(sizeof(*iter))) == NULL)
        return NULL;

    if (direction == DLIST_START_HEAD)
        iter->next = dl->head;
    else
        iter->next = dl->tail;
    iter->direction = direction;
    return iter;
}

/* Release the iterator memory. */
void dlist_destroy_iterator(dlist_iter *iter) {
    if (iter) 
        free(iter);
}

/* Create an iterator in the list private iterator structure. */
void dlist_rewind(dlist *dl, dlist_iter *iter) {
    iter->next = dl->head;
    iter->direction = DLIST_START_HEAD;
}

void dlist_rewind_tail(dlist *dl, dlist_iter *iter) {
    iter->next = dl->tail;
    iter->direction = DLIST_START_TAIL;
}

/* Return the next element of an iterator. It's valid to remove
   the currently returned element using dlist_delete_node(),
   but not to remove other elements. The function returns a pointer
   to the next element of the list, or NULL if there are no more
   elements, so the classical usage patter is :

   iter = dlist_get_iterator(dl, <direction>);
   while ((node = dlist_next(iter)) != NULL) {
        do_something(dlist_node_value(node));
   }
   */
dlist_node *dlist_next(dlist_iter *iter) {
    dlist_node *curr = iter->next;
    if (curr != NULL) {
        if (iter->direction == DLIST_START_HEAD)
            iter->next = curr->next;
        else 
            iter->next = curr->prev;
    }
    return curr;
}

/* Duplicate the whole list. On out of memory NULL is returned. 
   On success a copy of the original list is returned. The 'dup'
   method set with dlist_set_dup() function is used to copy the
   node value. Other wise the same pointer value of the original
   node is used as value of the copied node. */
dlist *dlist_dup(dlist *orig) {
    dlist *copy;
    dlist_iter *iter;
    dlist_node *node;

    if ((copy = dlist_init()) == NULL) {
        return NULL;
    }

    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    iter = dlist_get_iterator(orig, DLIST_START_HEAD);
    while ((node = dlist_next(iter)) != NULL) {
        void *value;
        if (copy->dup) {
            value = copy->dup(node->value);
            if (value == NULL) {
                dlist_destroy(copy);
                dlist_destroy_iterator(iter);
                return NULL;
            }
        } else {
            value = node->value;
        }
        if (dlist_add_node_tail(copy, value) == NULL) {
            dlist_destroy(copy);
            dlist_destroy_iterator(iter);
            return NULL;
        }
    } 
    dlist_destroy_iterator(iter);
    return copy;
}

/* Search the list for a node matching a given key. The match is 
   performed using the 'match' method set with dlist_set_match().
   if no 'match' method is set, the 'value' pointer of every node
   is directly compared with the 'key' pointer. On success the 
   first matching node pointer is returned (search starts from 
   head). If no matching node exists, NULL is returned. */
dlist_node *dlist_search_key(dlist *dl, void *key) {
    dlist_iter *iter = NULL;
    dlist_node *node = NULL;
    iter = dlist_get_iterator(dl, DLIST_START_HEAD);
    while ((node = dlist_next(iter)) != NULL) {
        if (dl->match) {
            if (dl->match(node->value, key)) {
                dlist_destroy_iterator(iter);
                return node;
            }
        } else {
            if (key == node->value) {
                dlist_destroy_iterator(iter);
                return node;
            }
        }
    }
    dlist_destroy_iterator(iter);
    return NULL;
}

/* Return the element at the specified zero-based index where 0
   is the head, 1 is the element next to head and so on. Negative
   integers are used in order to count from the tail, -1 is the
   last element, -2 the penultimante and so on. If the index is 
   out of range NULL is returned. */
dlist_node *dlist_index(dlist *dl, int index) {
    dlist_node *node;

    if (index < 0) {
        index = (-index) - 1;
        node = dl->tail;
        while (index-- && node) {
            node = node->prev;
        }
    } else {
        node = dl->head;
        while (index-- && node) {
            node = node->next;
        }
    }
    return node;
}
