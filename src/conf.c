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
#include "conf.h"

#define CONF_SLOTS_INITIAL_NUM      100
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

int conf_init(conf_t *conf, const char *filename) {
    int n;
    int ret = 0;
    FILE *fp;
    char buf[MAX_LINE];
    conf_entry_t *pentry;
    conf_entry_t **ptemp;
    unsigned char *field[2];

    if (!(fp = fopen(filename, "r"))) {
        perror("fopen failed");
        return -1;
    }

    if (!conf->list) {
        conf->size = 0;
        conf->slots = 0;
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
                DIR *dir;
                struct entry *ent;
                struct stat st;
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
                
                while ((ent = readdir(dir)) != 0) {
                    if (stat((const char *)ent->d_name, &st) != 0) {
                        ret = -1;
                        goto error;
                    }

                    if (S_ISDIR(st.st_mode)) {
                        continue;
                    }

                    if (!(rc = fnmatch(bname, ent->d_name, FNM_PATHNAME|FNM_PERIOD))) {
                        char fullpath[256];
                        snprintf(fullpath, 255, "%s/%s", dname, ent->d_name);
                        if (conf_init(conf, (const char *)fullpath) != 0) {
                            ret = -1;
                            goto error;
                        }
                    } else if (ret == FNM_NOMATCH) {
                        continue;
                    } else {
                        ret = -1;
                        goto error;
                    }
                }
                continue;
            }
            
            pentry = (conf_entry_t*)malloc(sizeof(conf_entry_t));
            if (!pentry) {
                fprintf(stderr, "malloc failed\n");
                ret = -1;
                goto error;
            }
            pentry->key = strdup((char *)field[0]);
            pentry->value = strdup((char *)field[1]);

            if (conf->size == conf->slots) {
                ptemp = (conf_entry_t **)realloc(conf->list, 
                    sizeof(conf_entry_t*) *
                    (conf->slots + CONF_SLOTS_INITIAL_NUM));
                if (!ptemp) {
                    fprintf(stderr, "realloc failed\n");
                    ret = -1;
                    goto error;
                }
                conf->list = ptemp;
                conf->slots += CONF_SLOTS_INITIAL_NUM;
            }
            conf->list[conf->size++] = pentry;
        }
    }
error:
    fclose(fp);
    if (ret == -1) {
        conf_free(conf);
    }
    return ret;
}


void conf_free(conf_t *conf) {
    int i;
    for (i = 0; i < conf->size; ++i) {
        if (conf->list[i]) {
            free(conf->list[i]);
            conf->list[i] = NULL;
        } else {
            break;
        }
    }
    free(conf->list);
}

void conf_dump(conf_t *conf) {
    int i = 0;
    for (i = 0; i < conf->size; ++i) {
        if (conf->list[i]) {
            printf("%-30s %-20s\n", conf->list[i]->key,
                    conf->list[i]->value);
        } else {
            break;
        }
    }
}

/* When key not found in conf, default value was returned. */
int conf_get_int_value(conf_t *conf, const char *key, int def) {
    int i;
    for (i = 0; i < conf->size; ++i) {
        if (!strcasecmp(key, conf->list[i]->key)) {
            return str2int(conf->list[i]->value, def);
        }
    }

    return def;
}

/* When key not found in conf, default value was returned. */
char * conf_get_str_value(conf_t *conf, const char *key, 
        char *def) {
    int i;
    for (i = 0; i < conf->size; ++i) {
        if (!strcasecmp(key, conf->list[i]->key)) {
            return conf->list[i]->value;
        }
    }

    return def;
}

#ifdef conf_TEST
int main(int argc, char *argv[]) {
    conf_t   conf;
    int i;
//    char test[] = "a#sys1.dev.corp.qihoo.net#1#fenggu_test#6#>#1#2#0.1";
    unsigned char test[] = "a#votdb1.ipt.dxt.qihoo.net#160#add_agent_mon#25#ROOT_URATE#>#3#1#6#0#根分区使用率";
    unsigned char *field[9];
/*
    if (conf_init(&conf, argv[1]) != 0) {
        fprintf(stderr, "conf_init error\n");
        exit(1);
    }
    conf_dump(&conf);
    conf_free(&conf);
*/
    if (str_explode("#", test, field, 12) != 12) {
        fprintf(stderr, "str_explode failed");
        exit(1);
    }

    for (i = 0; i < 12; i++) {
        printf("%d:%s\n", i, field[i]);
    }
    exit(0);
}
#endif /* conf_TEST */
