#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <alloca.h>
#include <libgen.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "hash.h"
#include "conf.h"

#define CONF_TYPE_ARRAY             1
#define CONF_TYPE_BLOCK             2

#define MAX_LINE                    1024

/* "\t\n\r " */
static const unsigned char default_ifs[256] = 
    { [9]=1, [10]=1, [13]=1, [32]=1 };

int str_explode(const unsigned char *ifs, unsigned char *buf, 
        unsigned char *field[], int n) {
    int i = 0;
    unsigned char *tempifs;

    /* When ifs is NULL, use the default blanks. If the first
       byte is NULL, use the IFS table, otherwise, use the IFS
       array as a separator table. */
    if (ifs == NULL) {
        ifs = default_ifs;
    } else if (*ifs) {
        tempifs = (unsigned char *)alloca(256);
        memset((void*)tempifs, 0, 256);
        while (*ifs) {
            tempifs[*ifs++] = 1;
        }
        ifs = tempifs;
    } 

    i = 0;
    while (1) {
        /* Trim the leading separators */
        while (ifs[*buf]) {
            buf++;
        }

        if (!*buf) { 
            break;
        }

        field[i++] = buf;

        if (i >= n) { /* Process the last field. */
            buf += strlen((char *)buf) - 1;
            while (ifs[*buf]) {
                --buf;
            }
            *(buf + 1) = '\0';
            break;
        }

        while (*buf && !ifs[*buf]) {
            ++buf;
        }

        if (!*buf) {
            break;
        }
        *buf++ = '\0';
    }
    return i;
}

static int str2int(const char *strval, int def) {
    int ret = def;

    if (isdigit(strval[0]) || (strval[0] == '-' && isdigit(strval[1]))) {
        return strtol(strval, NULL, 10);
    }

    if (!strcasecmp(strval, "on")) {
        ret = 1;
    } else if (!strcasecmp(strval, "off")) {
        ret = 0;
    } else if (!strcasecmp(strval, "yes")) {
        ret = 1;
    } else if (!strcasecmp(strval, "no")) {
        ret = 0;
    } else if (!strcasecmp(strval, "true")) {
        ret = 1;
    } else if (!strcasecmp(strval, "false")) {
        ret = 0;
    } else if (!strcasecmp(strval, "enable")) {
        ret = 1;
    } else if (!strcasecmp(strval, "disable")) {
        ret = 0;
    } else if (!strcasecmp(strval, "enabled")) {
        ret = 1;
    } else if (!strcasecmp(strval, "disabled")) {
        ret = 0;
    }

    return ret;
}


static void *key_dup(const void *key) {
    return strdup((char *)key);
}

static int key_cmp(const void *key1, 
        const void *key2) {
    return strcmp((char *)key1, (char *)key2) == 0;
}

static void free_key(void *key) {
    free(key);
}

static void free_val(void *val) {
    conf_value_t *cv = (conf_value_t *)val;
    conf_entry_t *he, *next;
    if (cv->type == CONF_TYPE_ARRAY) {
        he = (conf_entry_t *)cv->value;
        while (he) {
            next = he->next;
            free(he->value);
            free(he);
            he = next;
        }
        free(cv);
    } else if (cv->type == CONF_TYPE_BLOCK) {
        /* TODO */
        exit(1);
    } else {
        /* never get here */
        exit (1);
    }
}

/* Before calling this function first time, please
 * initialize the `conf_t' structure with zero. 
 * such as 'conf_t conf = {};' */
int conf_init(conf_t *conf, const char *filename) {
    int n;
    int ret = 0;
    FILE *fp;
    DIR *dir = NULL;
    char buf[MAX_LINE];
    unsigned char *field[2];
    char resolved_path1[PATH_MAX];
    char resolved_path2[PATH_MAX];
    conf_value_t *cv;

    if (!realpath(filename, resolved_path1)) {
        fprintf(stderr, "%s\n", strerror(errno));
        return -1;
    }

    if (!(fp = fopen(filename, "r"))) {
        perror("fopen failed");
        return -1;
    }

    if (!conf->ht) {
        conf->ht = hash_create(HASH_INIT_SLOTS);
        if (!conf->ht) {
            perror("hash_create failed");
            return -1;
        }

        HASH_SET_KEYCPY(conf->ht, key_dup);
        /* HASH_SET_VALCPY(conf->ht, val_dup); */
        HASH_SET_FREE_KEY(conf->ht, free_key);
        HASH_SET_FREE_VAL(conf->ht, free_val);
        HASH_SET_KEYCMP(conf->ht, key_cmp);
    }
 
    while (fgets(buf, MAX_LINE, fp)) {
        n = strlen(buf);
        if (buf[n - 1] == '\n') {
            buf[n - 1] = '\0';
        }

        if (*buf != '#' && str_explode(NULL, 
                    (unsigned char*)buf, field, 2) == 2) {
            /* Process `include' directive. */
            if (!strcmp((char *)field[0], "include")) {
                char dirc[256];
                char basec[256];
                char *dname;
                char *bname;
                struct dirent *entry;
                int rc;

                strncpy(dirc, (const char *)field[1], 255);
                strncpy(basec, (const char *)field[1], 255);
                dname = dirname(dirc);
                bname = basename(basec);

                if (!strcmp(bname, ".") || !strcmp(bname, "..") 
                        || !strcmp(bname, "/")) {
                    ret = -1;
                    goto error;
                }

                if (!(dir = opendir(dname))) {
                    ret = -1;
                    goto error;
                }
                
                while ((entry = readdir(dir))) {
                    char fullpath[256];
#ifdef _DIRENT_HAVE_D_TYPE
                    if (entry->d_type == DT_DIR) {
                        continue;
                    }
#else
                    struct stat st;
                    if (lstat((const char *)entry->d_name, &st) != 0) {
                        ret = -1;
                        goto error;
                    }

                    if (S_ISDIR(st.st_mode)) {
                        continue;
                    }
#endif /* _DIRENT_HAVE_D_TYPE */
                    snprintf(fullpath, 255, "%s/%s", dname, 
                        entry->d_name);

                    if (!realpath(fullpath, resolved_path2)) {
                        fprintf(stderr, "%s\n", strerror(errno));
                        ret = -1;
                        goto error;
                    }

                    if (!strcmp(resolved_path1, resolved_path2)) {
                        /* skip the same file */
                        continue;
                    }

                    if (!(rc = fnmatch(bname, entry->d_name, 
                                    FNM_PATHNAME | FNM_PERIOD))) {
                        if (conf_init(conf, (const char *)fullpath) != 0) {
                            ret = -1;
                            goto error;
                        }
                    } else if (rc == FNM_NOMATCH) {
                        continue;
                    } else {
                        ret = -1;
                        goto error;
                    }
                }
                closedir(dir);
                dir = NULL;
                continue;
            }
            
            /* process a key/value config */
            cv = (conf_value_t*)hash_get_val(conf->ht, (void *)field[0]);
            if (!cv) {
                conf_entry_t *ce;

                ce = calloc(1, sizeof(conf_entry_t));
                if (!ce) {
                    ret = -1;
                    goto error;
                }
                ce->value = strdup((char *)field[1]);
                ce->next = NULL;

                cv = calloc(1, sizeof(conf_value_t));
                if (!cv) {
                    free(ce);
                    ret = -1;
                    goto error;
                }
                cv->type = CONF_TYPE_ARRAY;
                cv->value = ce;
                if (hash_insert(conf->ht, (void *)field[0], cv) != 0) {
                    free(ce);
                    free(cv);
                    ret = -1;
                    goto error;
                }
            } else {
                if (cv->type == CONF_TYPE_ARRAY) {
                    conf_entry_t *ce;
                    ce = calloc(1, sizeof(conf_entry_t));
                    if (!ce) {
                        ret = -1;
                        goto error;
                    }
                    ce->value = strdup((char *)field[1]);
                    ce->next = (conf_entry_t*)(cv->value);
                    cv->value = ce;
                } else {
                    ret = -1;
                    goto error;
                }
            }
        }
    }

error:
    fclose(fp);
    if (dir) {
        closedir(dir);
    }
    if (ret == -1) {
        conf_free(conf);
    }
    return ret;
}


void conf_free(conf_t *conf) {
    hash_free(conf->ht);
}

/* When key not found in conf, default value was returned. */
int conf_get_int_value(conf_t *conf, const char *key, int def) {
    conf_entry_t *ce;
    conf_value_t *cv = (conf_value_t*)hash_get_val(conf->ht, key);

    if (!cv) {
        return def;
    }

    if (cv->type == CONF_TYPE_BLOCK) {
        return def;
    } else if (cv->type == CONF_TYPE_ARRAY) {
        ce = (conf_entry_t *)cv->value;
    } else {
        return def;
    }

    if (ce) {
        return str2int(ce->value, def);
    }

    return def;
}

/* When key not found in conf, default value was returned. */
char * conf_get_str_value(conf_t *conf, const char *key, 
        char *def) {
    conf_entry_t *ce;
    conf_value_t *cv = (conf_value_t*)hash_get_val(conf->ht, key);

    if (!cv) {
        return def;
    }

    if (cv->type == CONF_TYPE_BLOCK) {
        return def;
    } else if (cv->type == CONF_TYPE_ARRAY) {
        ce = (conf_entry_t *)cv->value;
    } else {
        return def;
    }

    if (ce) {
        return ce->value;
    }

    return def;
}

int conf_array_foreach(conf_t *conf,
        char *key,
        int (*foreach)(void *key, void *value, void *userptr),
        void *userptr) {
    conf_entry_t *ce, *next;
    conf_value_t *cv = (conf_value_t*)hash_get_val(conf->ht, key);

    if (!cv) {
        return -1;
    }

    if (cv->type == CONF_TYPE_BLOCK) {
        return -1;
    } else if (cv->type == CONF_TYPE_ARRAY) {
        ce = (conf_entry_t *)cv->value;
        while (ce) {
            next = ce->next;
            if (foreach(key, ce->value, userptr) != 0) {
                return -1;
            }
            ce = next;
        }
        return 0;
    } else {
        return -1;
    }
}

static int print_conf(void *key, void *value, void *userptr) {
    printf("%-20s %-30s\n", (char *)key, (char *)value);
    return 0;
}

static int conf_print_foreach(const hash_entry_t *he, void *userptr) {
    conf_value_t * cv = (conf_value_t*)he->val;
    if (cv->type == CONF_TYPE_ARRAY) {
        return conf_array_foreach((conf_t*)userptr, he->key,  
                print_conf, NULL);
    } else {
        return 0;
    }
}

void conf_dump(conf_t *conf) {
    hash_foreach(conf->ht, conf_print_foreach, (void *)conf);
}

#ifdef CONF_TEST_MAIN
/* gcc conf.c hash.c -DCONF_TEST_MAIN -I../inc */
int main(int argc, char *argv[]) {
    conf_t   conf = {};

    if (argc < 2) {
        fprintf(stderr, "usage: ./a.out <conf_file>\n");
        exit(1);
    }

    if (conf_init(&conf, argv[1]) != 0) {
        fprintf(stderr, "conf_init error\n");
        exit(1);
    }
    conf_dump(&conf);

    printf("PORT: %d\n", conf_get_int_value(&conf, "porta", 7777));
    printf("LOG_NAME: %s\n", conf_get_str_value(&conf, "log_name", "NULL"));

    conf_free(&conf);
    exit(0);
}
#endif /* CONF_TEST_MAIN */
