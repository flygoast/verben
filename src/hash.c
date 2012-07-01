#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include "hash.h"

static unsigned long next_power(unsigned long size) {
    unsigned long i = HASH_INIT_SLOTS;
    if (size >= LONG_MAX) {
        return LONG_MAX;
    }

    while (1) {
        if (i >= size) {
            return i;
        }
        i *= 2;
    }
}

static unsigned int hash_func(const void *key) {
    unsigned int h = 5381;
    char *ptr = (char *)key;

    while (*ptr != '\0') {
        h = (h + (h << 5)) + (*ptr++);
    }

    return h;
}

/* the foreach function to use with the hash_clear() macro */
static int _hash_delete_foreach(const hash_entry_t *he, void *ht) {
    if (hash_delete(ht, he->key) != 0) {
        return -1;
    }
    return 0;
}

/* the foreach handler to duplicate the hash table */
static int _hash_dup_foreach(const hash_entry_t *he, void *copy) {
    if (hash_insert((hash_t *)copy, he->key, he->val) != 0) {
        return -1;
    }
    return 0;
}

/* Create a new hash table. The slots is the number of slots 
 * to create the hash table with. */
hash_t *hash_create(unsigned int slots) {
    hash_t *ht = calloc(1, sizeof(*ht));

    if (!ht) {
        return NULL;
    }

    if (hash_init(ht, slots) != 0) {
        free(ht);
        ht = NULL;
        return NULL;
    }

    return ht;
}

/* Initialize an allocated hash table. */
int hash_init(hash_t *ht, unsigned int slots) {
    assert(ht);

    ht->slots = next_power(slots);
    ht->count = 0;

    ht->data = (hash_entry_t **)calloc(ht->slots, sizeof(hash_entry_t *));
    if (!ht->data) {
        return -1;
    }
    return 0;
}

/* Insert a key/value pair into the hash table. */
int hash_insert(hash_t *ht, const void *key, const void *val) {
    hash_entry_t *he;
    unsigned int hash, i;

    assert(ht && key && val);

    if ((ht->count /  ht->slots) >= HASH_RESIZE_RATIO) {
        if (hash_resize(ht) != 0) {
            return -1;
        }
    }

    hash = hash_func(key);
    i = hash % ht->slots;

    if ((he = ht->data[i])) {
        while (he) {
            if (HASH_CMP_KEYS(ht, key, he->key)) {
                /* replace the value */
                HASH_FREE_VAL(ht, he);
                HASH_SET_VAL(ht, he, (void *)val);
                return 0;
            }
            he = he->next;
        }
    }

    he = calloc(1, sizeof(*he));
    if (!he) {
        return -1;
    }

    HASH_SET_KEY(ht, he, (void *)key);
    HASH_SET_VAL(ht, he, (void *)val);

    he->next = ht->data[i];
    ht->data[i] = he;
    ++ht->count;
    return 0;
}

/* Get an entry in the hash table. */
void *hash_get_val(hash_t *ht, const void *key) {
    unsigned int hash, i;
    hash_entry_t *he;

    hash = hash_func(key);
    i = hash % ht->slots;

    if ((he = ht->data[i])) {
        while (he) {
            if (HASH_CMP_KEYS(ht, key, he->key)) {
                return he->val;
            }

            he = he->next;
        }
    }
    return NULL;
}

/* remove an entry from a hash table */
int hash_delete(hash_t *ht, const void *key) {
    unsigned int index;
    hash_entry_t *he, *prev;

    index = hash_func(key) % ht->slots;

    if ((he = ht->data[index])) {
        prev = NULL;
        while (he) {
            if (HASH_CMP_KEYS(ht, key, he->key)) {
                /* remove the entry from the linked list */
                if (prev) {
                    prev->next = he->next;
                } else {
                    ht->data[index] = he->next;
                }

                HASH_FREE_KEY(ht, he);
                HASH_FREE_VAL(ht, he);
                free(he);
                --ht->count;
                return 0;
            }
            prev = he;
            he = he->next;
        }
    }
    return -1;
}

/* execute a function for each key in a hash table */
int hash_foreach(hash_t *ht, 
        int (*foreach)(const hash_entry_t *, void *userptr),
        void *userptr) {
    unsigned int i;
    hash_entry_t *he, *next;

    assert(foreach);

    for (i = 0; i < ht->slots; ++i) {
        if ((he = ht->data[i])) {
            while (he) {
                int ret;
                next = he->next;
                if ((ret = foreach(he, userptr)) != 0) {
                    return -1;
                }
                he = next;
            }
        }
    }
    return 0;
}

/* destroy a hash table */
void hash_destroy(hash_t *ht) {
    hash_clear(ht);
    free(ht->data);
    ht->data = NULL;

    ht->count = 0;
    ht->slots = 0;

    ht->keycmp = NULL;
    ht->keycpy = NULL;
    ht->valcpy = NULL;
    ht->free_key = NULL;
    ht->free_val = NULL;
}

/* free a hash table */
void hash_free(hash_t *ht) {
    hash_destroy(ht);
    free(ht);
}

/* double size of the hash table */
int hash_resize(hash_t *ht) {
    hash_entry_t **tmp, *he, *next;
    unsigned int i, slots, h;
    int ret;

    tmp = ht->data;
    ht->data = NULL;
    slots = ht->slots;

    if ((ret = hash_init(ht, slots * 2)) != 0) {
        return -1;
    }

    /* tmp retails the proper pointer for now */
    for (i = 0; i < slots; ++i) {
        if ((he = tmp[i])) {
            while (he) {
                next = he->next;
                h = hash_func(he->key) % ht->slots; /* new hash value */
                /* insert into new hash table */
                he->next = ht->data[h];
                ht->data[h] = he;
                ht->count++;
                he = next;
            }
        }
    }

    free(tmp);
    return 0; 
}

/* duplicate a hash table */
hash_t *hash_dup(hash_t *ht) {
    hash_t *copy;
    copy = hash_create(ht->slots);
    if (!copy) {
        return NULL;
    }

    HASH_SET_KEYCPY(copy, ht->keycpy);
    HASH_SET_VALCPY(copy, ht->valcpy);
    HASH_SET_FREE_KEY(copy, ht->free_key);
    HASH_SET_FREE_VAL(copy, ht->free_val);
    HASH_SET_KEYCMP(copy, ht->keycmp);

    hash_foreach(ht, _hash_dup_foreach, copy);
    return copy;
}

#ifdef HASH_TEST_MAIN
static int _hash_print_foreach(const hash_entry_t *he, void *userptr) {
    printf("%s => %s\n", (char *)he->key, (char *)he->val);
    return 0;
}

static void hash_dump(hash_t *ht) {
    hash_foreach(ht, _hash_print_foreach, NULL);
}

static int randstring(char *target, unsigned int min, unsigned int max) {
    int p = 0;
    int len = min + rand() % (max - min + 1);
    int minval, maxval;
    minval = 'A';
    maxval = 'z';
    while (p < len) {
        target[p++] = minval + rand() % (maxval - minval + 1);
    }
    return len;
}

static void *_demo_dup(const void *key) {
    return strdup((char *)key);
}

static int _demo_cmp(const void *key1, 
        const void *key2) {
    return strcmp((char *)key1, (char *)key2) == 0;
}

static void _demo_destructor(void *key) {
    free(key);
}

int main(int argc, char *argv[]) {
    char buf[32];
    char buf2[32];
    int i;
    int len;
    hash_t *ht, *htcpy;

    srand(time(NULL));
    ht = hash_create(16);
    HASH_SET_KEYCPY(ht, _demo_dup);
    HASH_SET_VALCPY(ht, _demo_dup);
    HASH_SET_FREE_KEY(ht, _demo_destructor);
    HASH_SET_FREE_VAL(ht, _demo_destructor);
    HASH_SET_KEYCMP(ht, _demo_cmp);

    for (i = 0; i < 10000; ++i) {
        len = randstring(buf, 1, sizeof(buf) - 1);
        buf[len] = '\0';
        len = randstring(buf2, 1, sizeof(buf2) - 1);
        buf2[len] = '\0';
        hash_insert(ht, buf, buf2);
    }

    hash_dump(ht);
    htcpy = hash_dup(ht);
    hash_free(ht);
    printf("================================\n\n\n");
    hash_dump(htcpy);
    hash_free(htcpy);
    exit(0);
}
#endif /* HASH_TEST_MAIN */
