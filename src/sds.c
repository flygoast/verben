/* SDSLib, A C dynamic strings library. */
#define SDS_ABORT_ON_OOM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sds.h"

static void sds_oom_abort(void) {
    fprintf(stderr, "SDS: Out of memory (SDS_ABORT_ON_OOM defined)\n");
    abort();
}

sds sdsnewlen(const void *init, size_t initlen) {
    struct sdshdr *sh;

    sh = malloc(sizeof(struct sdshdr) + initlen + 1);
#ifdef SDS_ABORT_ON_OOM
    if (!sh) sds_oom_abort();
#else
    if (!sh) return NULL;
#endif
    sh->len = initlen;
    sh->free = 0;

    if (initlen) {
        if (init) {
            memcpy(sh->buf, init, initlen);
        } else {
            memset(sh->buf, 0, initlen);
        }
    }
    sh->buf[initlen] = '\0';
    return (char *)sh->buf;
}

sds sdsempty(void) {
    return sdsnewlen("", 0);
}

sds sdsnew(const char *init) {
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}

sds sdsdup(const sds s) {
    return sdsnewlen(s, sdslen(s));
}

void sdsfree(sds s) {
    if (s == NULL) {
        return;
    }

    free(s - sizeof(struct sdshdr));
}

void sdsupdatelen(sds s) {
    struct sdshdr *sh = (void *)(s - sizeof(struct sdshdr));
    int reallen = strlen(s);
    sh->free += (sh->len - reallen);
    sh->len = reallen;
}

void sdsclear(sds s) {
    struct sdshdr *sh = (void *)(s - sizeof(struct sdshdr));
    sh->free += sh->len;
    sh->len = 0;
    sh->buf[0] = '\0';
}

static sds sds_make_room_for(sds s, size_t addlen) {
    struct sdshdr *sh, *newsh;
    size_t free = sdsavail(s);
    size_t len, newlen;

    if (free >= addlen) {
        return s;
    }

    len = sdslen(s);
    sh = (void *)(s - sizeof(struct sdshdr));
    newlen = (len + addlen) * 2;
    newsh = realloc(sh, sizeof(struct sdshdr) + newlen + 1);
#ifdef SDS_ABORT_ON_OOM
    if (!newsh) sds_oom_abort();
#else
    if (!newsh) return NULL;
#endif
    newsh->free = newlen - len;
    return newsh->buf;
}

/* Grow the sds to have the specified length. Bytes that were  not
   part of the original length of the sds will be set to zero. */
sds sdsgrowzero(sds s, size_t len) {
    struct sdshdr *sh = (void *)(s - sizeof(struct sdshdr));
    size_t totlen, curlen = sh->len;

    if (len <= curlen) {
        return s;
    }

    s = sds_make_room_for(s, len - curlen);
    if (!s) {
        return NULL;
    }

    /* Make sure added region doesn't contain garbage */
    sh = (void *)(s - sizeof(struct sdshdr));
    /* Also set trailing \0 byte */
    memset(s + curlen, 0, (len - curlen + 1)); 
    totlen = sh->len + sh->free;
    sh->len = len;
    sh->free = totlen - sh->len;
    return s;
}

sds sdscatlen(sds s, void *t, size_t len) {
    struct sdshdr *sh;
    size_t curlen = sdslen(s);

    s = sds_make_room_for(s, len);
    if (!s) {
        return NULL;
    }

    sh = (void *)(s - sizeof(struct sdshdr));
    memcpy(s + curlen, t, len);
    sh->len = curlen + len;
    sh->free = sh->free - len;
    s[curlen + len] = '\0';
    return s;
}

sds sdscat(sds s, char *t) {
    return sdscatlen(s, t, strlen(t));
}

sds sdscatsds(sds s, sds t) {
    return sdscatlen(s, t, sdslen(t));
}

sds sdscpylen(sds s, char *t, size_t len) {
    struct sdshdr *sh = (void *)(s - sizeof(struct sdshdr));
    size_t totlen = sh->free + sh->len;

    if (totlen < len) {
        s = sds_make_room_for(s, len - sh->len);
        if (!s) {
            return NULL;
        }
        sh = (void *)(s - sizeof(struct sdshdr));
        totlen = sh->free + sh->len;
    }
    memcpy(s, t, len);
    s[len] = '\0';
    sh->len = len;
    sh->free = totlen - len;
    return s;
}

sds sdscpy(sds s, char *t) {
    return sdscpylen(s, t, strlen(t));
}

sds sdscatvprintf(sds s, const char *fmt, va_list ap) {
    va_list cpy;
    char *buf, *t;
    size_t buflen = 16;

    while (1) {
        buf = malloc(buflen);
#ifdef SDS_ABORT_ON_OOM
        if (!buf) {
            sds_oom_abort();
        }
#else 
        if (!buf) {
            return NULL;
        }
#endif

        buf[buflen - 2] = '\0';
        va_copy(cpy, ap);
        vsnprintf(buf, buflen, fmt, cpy);
        if (buf[buflen - 2] != '\0') {
            free(buf);
            buflen *= 2;
            continue;
        }
        break;
    }

    t = sdscat(s, buf);
    free(buf);
    return t;
}

sds sdscatprintf(sds s, const char *fmt, ...) {
    va_list ap;
    char *t;
    va_start(ap, fmt);
    t = sdscatvprintf(s, fmt, ap);
    va_end(ap);
    return t;
}

sds sdstrim(sds s, const char *cset) {
    struct sdshdr *sh = (void *)(s - sizeof(struct sdshdr));
    char *start, *end, *sp, *ep;
    size_t len;

    sp = start = s;
    ep = end = s + sdslen(s) - 1;
    /* ltrim */
    while (sp <= end && strchr(cset, *sp)) {
        sp++;
    }

    /* rtrim */
    while (ep > start && strchr(cset, *ep)) {
        ep--;
    }

    len = (sp > ep) ? 0 : ((ep - sp) + 1);
    if (sh->buf != sp) {
        memmove(sh->buf, sp, len);
    }
    sh->buf[len] = '\0';
    sh->free = sh->free + (sh->len - len);
    sh->len = len;
    return s;
}

sds sdsrange(sds s, int start, int end) {
    struct sdshdr *sh = (void *)(s - sizeof(struct sdshdr));
    size_t newlen, len = sdslen(s);

    if (len == 0) {
        return s;
    }

    if (start < 0) {
        start = len + start;
        if (start < 0) {
            start = 0;
        }
    }

    if (end < 0) {
        end = len + end;
        if (end < 0) {
            end = 0;
        }
    }

    newlen = (start > end) ? 0 : (end - start) + 1;
    if (newlen != 0) {
        if (start >= (signed int)len) {
            newlen = 0;
        } else if (end >= (signed int)len) {
            end = len - 1;
            newlen = (start > end) ? 0 : (end - start) + 1;
        }
    } else {
        start = 0;
    }
    if (start && newlen) {
        memmove(sh->buf, sh->buf + start, newlen);
    }
    sh->buf[newlen] = 0;
    sh->free = sh->free + (sh->len - newlen);
    sh->len = newlen;
    return s;
}

void sdstolower(sds s) {
    int len = sdslen(s), j;
    for (j = 0; j < len; ++j) {
        s[j] = tolower(s[j]);
    }
}

void sdstoupper(sds s) {
    int len = sdslen(s), j;
    for (j = 0; j < len; ++j) {
        s[j] = toupper(s[j]);
    }
}

int sdscmp(sds s1, sds s2) {
    size_t l1, l2, minlen;
    int cmp;
    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < l2) ? l1 : l2;
    cmp = memcmp(s1, s2, minlen);
    if (cmp == 0) {
        return l1 - l2;
    } 
    return cmp;
}

/* Split 's' with separator in 'sep'. An array of sds strings
   is returned. *count will be set by reference to the number
   of tokens returned.

   On out of memory, zero length string, zero length separator
   , NULL is returned.

   Note that 'sep' is able to split a string using a multi-character
   separator. For example sdssplit("foo_-_bar", "_-_"); will return
   two elements "foo" and "bar".

   This version of the function is binary-safe but requires length
   arguments. sdssplit() is just the same function but for 
   zero-terminated strings. */
sds *sdssplitlen(char *s, int len, char *sep, int seplen, int *count) {
    int elements = 0, slots = 5, start = 0, j;
    sds *tokens;

    if (seplen < 1 || len < 0) {
        return NULL;
    }

    tokens = malloc(sizeof(sds) * slots);
#ifdef SDS_ABORT_ON_OOM
    if (!tokens) sds_oom_abort();
#else 
    if (!tokens) return NULL;
#endif

    if (len == 0) {
        *count = 0;
        return tokens;
    }

    for (j = 0; j < (len - (seplen - 1)); ++j) {
        /* Make sure there is room for the next element and
           the final one */
        if (slots < elements + 2) {
            sds *newtokens;
            slots *= 2;
            newtokens = realloc(tokens, sizeof(sds) * slots);
            if (!newtokens) {
#ifdef SDS_ABORT_ON_OOM
                sds_oom_abort();
#else
                goto cleanup;
#endif 
            }
            tokens = newtokens;
        }

        /* search the separator */
        if ((seplen == 1 && *(s + j) == sep[0]) ||
                (memcmp(s + j, sep, seplen) == 0)) {
            tokens[elements] = sdsnewlen(s + start, j - start);
            if (tokens[elements] == NULL) {
#ifdef SDS_ABORT_ON_OOM
                sds_oom_abort();
#else
                goto cleanup;
#endif
            }
            ++elements;
            start = j + seplen;
            j = j + seplen - 1; /* skip the separator */
        }
    }
    /* Add the final element. We are sure there is room in the 
       token array. */
    tokens[elements] = sdsnewlen(s + start, len - start);
    if (tokens[elements] == NULL) {
#ifdef SDS_ABORT_ON_OOM
        sds_oom_abort();
#else
        goto cleanup;
#endif
    }
    ++elements;
    *count = elements;
    return tokens;

#ifndef SDS_ABORT_ON_OOM
cleanup:
    {
        int i;
        for (i = 0; i < elements; ++i) {
            sdsfree(tokens[i]);
        }
        free(tokens);
        return NULL;
    }
#endif /* SDS_ABORT_ON_OOM */
}

sds *sdssplit(char *s, char *sep, int *count) {
    return sdssplitlen(s, strlen(s), sep, strlen(sep), count);
}

void sdsfreesplitres(sds *tokens, int count) {
    if (!tokens) return;
    while (count--) {
        sdsfree(tokens[count]);
    }
    free(tokens);
}

sds sdsfromlonglong(long long value) {
    char buf[32], *p;
    unsigned long long v;
    v = (value < 0) ? -value : value;
    p = buf + 31; /* point to the last character */
    do {
        *p-- = '0' + (v % 10);
        v /= 10;
    } while (v);
    if (value < 0) *p-- = '-';
    ++p;
    return sdsnewlen(p, 32 - (p - buf));
}

sds sdscatrepr(sds s, char *p, size_t len) {
    s = sdscatlen(s, "\"", 1);
    while (len--) {
        switch (*p) {
        case '\\':
        case '"':
            s = sdscatprintf(s, "\\%c", *p);
            break;
        case '\n':
            s = sdscatlen(s, "\\n", 2);
            break;
        case '\r':
            s = sdscatlen(s, "\\r", 2);
            break;
        case '\t':
            s = sdscatlen(s, "\\t", 2);
            break;
        case '\a':
            s = sdscatlen(s, "\\a", 2);
            break;
        case '\b':
            s = sdscatlen(s, "\\b", 2);
            break;
        default:
            if (isprint(*p)) {
                s = sdscatprintf(s, "%c", *p);
            } else {
                s = sdscatprintf(s, "\\x%02x", (unsigned char)*p);
            } 
            break;
        }
        ++p;
    }
    return sdscatlen(s, "\"", 1);
}

/* Helper function for sdssplitargs() that returns non zero if 'c'
   is a valid hex digit. */
int is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F');
}

/* Helper function for sdssplitargs() that converts an hex digit
   into an integer from 0 to 15. */
int hex_digit_to_int(char c) {
    switch (c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default: return 0;
    }
}

/* Split a line into arguments, where every argument can be in the
   following programming-language REPL-alive form:

   foo bar "newline are supported\n" and "\xff\x00otherstuff"

   The number of arguments is stored into *argc, and an array of
   sds is returned. The caller should sdsfreesplitres() the sds
   array.

   Note that sdscatrepr() is able to convert back a string into
   a quoted string in the same format sdssplitargs() is able to 
   parse. */
sds *sdssplitargs(char *line, int *argc) {
    char *p = line;
    char *current = NULL;
    char **temp = NULL;
    char **vector = NULL;

    *argc = 0;
    while (1) {
        /* skip blanks */
        while (*p && isspace(*p)) ++p;
        if (*p) {
            /* get a token */
            int inq = 0; /* set to 1 if we are in "quotes" */
            int insq = 0; /* set to 1 if we are in 'single quotes' */
            int done = 0;

            if (current == NULL) {
                current = sdsempty();
            }

            while (!done) {
                if (inq) {
                    if (*p == '\\' && *(p + 1) == 'x' &&
                            is_hex_digit(*(p + 2)) &&
                            is_hex_digit(*(p + 3))) {
                        unsigned char byte;
                        byte = (hex_digit_to_int(*(p + 2)) * 16) + 
                            hex_digit_to_int(*(p + 3));
                        current = sdscatlen(current, (char *)&byte, 1);
                        p += 3;
                    } else if (*p == '\\' && *(p + 1)) {
                        char c;

                        p++;
                        switch (*p) {
                            case 'n': c = '\n'; break;
                            case 'r': c = '\r'; break;
                            case 't': c = '\t'; break;
                            case 'b': c = '\b'; break;
                            case 'a': c = '\a'; break;
                            default: c = *p; break;
                        }
                        current = sdscatlen(current, &c, 1);
                    } else if (*p == '"') {
                        /* Closing quoto mute be followed by a space
                           or nothing at all. */
                        if (*(p + 1) && !isspace(*(p + 1))) {
                            goto err;
                        }
                        done = 1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        goto err;
                    } else {
                        current = sdscatlen(current, p, 1);
                    }
                } else if (insq) {
                    if (*p == '\\' && *(p + 1) == '\'') {
                        ++p;
                        current = sdscatlen(current, "'", 1);
                    } else if (*p == '\'') {
                        /* closing quote must be followed by a space or
                           nothing at all. */
                        if (*(p + 1) && !isspace(*(p + 1))) {
                            goto err;
                        }
                        done = 1;
                    } else if (!*p) {
                        /* unterminated quotes */
                        goto err;
                    } else {
                        current = sdscatlen(current, p, 1);
                    }
                } else {
                    switch (*p) {
                    case ' ':
                    case '\n':
                    case '\r':
                    case '\t':
                    case '\0':
                        done = 1;
                        break;
                    case '"':
                        inq = 1;
                        break;
                    case '\'':
                        insq = 1;
                        break;
                    default:
                        current = sdscatlen(current, p, 1);
                        break;
                    }
                }
                if (*p) ++p;
            }
            /* Add the token to the vector */
            temp = realloc(vector, ((*argc) + 1) * sizeof(char *));
#ifdef SDS_ABORT_ON_OOM
            if (!temp) sds_oom_abort();
#else
            if (!temp) goto err;
#endif
            vector = temp;
            vector[*argc] = current;
            ++(*argc);
            current = NULL;
        } else {
            return vector;
        }
    }
err:
    while ((*argc)--) {
        sdsfree(vector[*argc]);
    }
    free(vector);
    if (current) {
        sdsfree(current);
    }

    return NULL;
}

#ifdef SDS_TEST_MAIN
#include <stdio.h>
#include "testhelp.h"

int main(void) {
    sds x = sdsnew("foo"), y;
    test_cond("Create a string and obtain the length", 
        sdslen(x) == 3 && memcmp(x, "foo\0", 4) == 0);
    sdsfree(x);

    x = sdsnewlen("foo", 2);
    test_cond("Create a string with specified length",
            sdslen(x) == 2 && memcmp(x, "fo\0", 3) == 0);

    x = sdscat(x, "bar");
    test_cond("Strings concatenation",
            sdslen(x) == 5 && memcmp(x, "fobar\0", 6) == 0);

    x = sdscpy(x, "a");
    test_cond("sdscpy() against an originally longer string",
            sdslen(x) == 1 && memcmp(x, "a\0", 2) == 0);

    x = sdscpy(x, "xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk");
    test_cond("sdscpy() against an originally shorter string",
            sdslen(x) == 33 &&
            memcmp(x, "xyzxxxxxxxxxxyyyyyyyyyykkkkkkkkkk\0", 34) == 0);
    sdsfree(x);

    x = sdscatprintf(sdsempty(), "%d", 123);
    test_cond("sdscatprintf() seems working in the base case",
            sdslen(x) == 3 && memcmp(x, "123\0", 4) == 0);
    sdsfree(x);

    x = sdstrim(sdsnew("xxciaoyyy"), "xy");
    test_cond("sdstrim() correctly trims characters",
            sdslen(x) == 4 && memcmp(x, "ciao\0", 5) == 0);

    y = sdsrange(sdsdup(x), 1, 1);
    test_cond("sdsrange(..., 1, 1)",
            sdslen(y) == 1 && memcmp(y, "i\0", 2) == 0);
    sdsfree(y);

    y = sdsrange(sdsdup(x), 1, -1);
    test_cond("sdsrange(..., 1, -1)",
            sdslen(y) == 3 && memcmp(y, "iao\0", 4) == 0);
    sdsfree(y);

    y = sdsrange(sdsdup(x), -2, -1);
    test_cond("sdsrange(..., -2, -1)",
            sdslen(y) == 2 && memcmp(y, "ao\0", 3) == 0);
    sdsfree(y);

    y = sdsrange(sdsdup(x), 2, 1);
    test_cond("sdsrange(..., 2, 1)",
            sdslen(y) == 0 && memcmp(y, "\0", 1) == 0);
    sdsfree(y);

    y = sdsrange(sdsdup(x), 100, 100);
    test_cond("sdsrange(..., 100, 100)",
            sdslen(y) == 0 && memcmp(y, "\0", 1) == 0);
    sdsfree(y);
    sdsfree(x);

    x = sdsnew("foo");
    y = sdsnew("foa");
    test_cond("sdscmp(foo, foa)", sdscmp(x, y) > 0);
    sdsfree(y);
    sdsfree(x);

    x = sdsnew("bar");
    y = sdsnew("bar");
    test_cond("sdscmp(aar, bar)", sdscmp(x, y) == 0);

    sdsfree(y);
    sdsfree(x);

    x = sdsnew("aar");
    y = sdsnew("bar");
    test_cond("sdscmp(aar, bar)", sdscmp(x, y) < 0);

    {
        sds *vector;
        int argc;
        char *line = "foo bar \"newline are supported\n\""
            " and \"\\xff\\x00otherstuff\"";
        vector = sdssplitargs(line, &argc);
        test_cond("sdssplitargs(line, &argc)", 
                argc == 5);
        sdsfreesplitres(vector, argc);
    }

    test_report();
    exit(0);
}
#endif /* SDS_TEST_MAIN */
