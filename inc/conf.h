#ifndef __CONF_H_INCLUDED__
#define __CONF_H_INCLUDED__

#include "hash.h"

typedef struct conf_entry {
    char                *value;
    struct conf_entry   *next;
} conf_entry_t;

typedef struct conf_value {
    unsigned char   type;
    void            *value;
} conf_value_t;

typedef struct conf_t {
    hash_t          *ht;
} conf_t;

extern int conf_array_foreach(conf_t *conf,
        char *key,
        int (*foreach)(void *key, void *value, void *userptr),
        void *userptr);

extern int str_explode(const unsigned char *ifs, unsigned char *buf, 
        unsigned char *field[], int n);
extern int conf_init(conf_t *conf, const char *filename);
extern void conf_free(conf_t *conf);
extern int conf_get_int_value(conf_t *conf, const char *key, int def);
extern char *conf_get_str_value(conf_t *conf, 
    const char *key, char *def);

#endif /* __CONF_H_INCLUDED__ */
