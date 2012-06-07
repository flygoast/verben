#ifndef __CONFIG_H_INCLUDED__
#define __CONFIG_H_INCLUDED__

typedef struct config_entry {
    char *key;
    char *value;
} config_entry_t;

typedef struct config_t {
    config_entry_t **list;
    unsigned int    size;
    unsigned int    slots;
} config_t;

extern int config_init(config_t *conf, const char *filename);
extern void config_free(config_t *conf);
extern int config_get_int_value(config_t *conf, const char *key, int def);
extern char *config_get_str_value(config_t *conf, 
    const char *key, char *def);

#endif /* __CONFIG_H_INCLUDED__ */
