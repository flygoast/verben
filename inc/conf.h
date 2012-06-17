#ifndef __CONF_H_INCLUDED__
#define __CONF_H_INCLUDED__

typedef struct conf_entry {
    char *key;
    char *value;
} conf_entry_t;

typedef struct conf_t {
    conf_entry_t    **list;
    unsigned int    size;
    unsigned int    slots;
} conf_t;

extern int conf_init(conf_t *conf, const char *filename);
extern void conf_free(conf_t *conf);
extern int conf_get_int_value(conf_t *conf, const char *key, int def);
extern char *conf_get_str_value(conf_t *conf, 
    const char *key, char *def);

#endif /* __CONF_H_INCLUDED__ */
