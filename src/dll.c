#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "dll.h"

#define DLFUNC_NO_ERROR(h, v, name) do { \
    *(void **)(v) = dlsym(h, name); \
    dlerror(); \
} while (0)

#define DLFUNC(h, v, name) do { \
    *(void **)(v) = dlsym(h, name); \
    if ((error = dlerror()) != NULL) { \
        dlclose(h); \
        h = NULL; \
        return rc; \
    } \
} while (0)

int load_so(void **phandle, symbol_t *sym, const char *filename) {
    char    *error;
    int     rc = -1;
    int     i = 0;
    
    *phandle = dlopen(filename, RTLD_NOW);
    if ((error = dlerror()) != NULL) {
        fprintf(stderr, "dlopen so file failed:%s\n", error);
        return rc;
    }

    while (sym[i].sym_name) {
        if (sym[i].no_error) {
            DLFUNC_NO_ERROR(*phandle, sym[i].sym_ptr, sym[i].sym_name);
        } else {
            DLFUNC(*phandle, sym[i].sym_ptr, sym[i].sym_name);
        }
        ++i;
    }
    
    rc = 0;
    return rc;
}

void unload_so(void **phandle) {
    if (*phandle != NULL) {
        dlclose(*phandle);
        *phandle = NULL;
    }
}

#ifdef DLL_TEST
typedef struct dll_func_struct {
    int (*handle_init)(const void *data, int proc_type);
    int (*handle_fini)(const void *data, int proc_type);
    int (*handle_task)(const void *data);
} dll_func_t;

int main(int argc, char *argv[]) {
    void * handle;
    dll_func_t dll;
    symbol_t syms[] = {
        {"handle_init", (void **)&dll.handle_init, 1},
        {"handle_fini", (void **)&dll.handle_fini, 1},
        {"handle_task", (void **)&dll.handle_task, 0},
        {NULL, NULL, 0}
    };

    if (argc < 2) {
        fprintf(stderr, "Invalid arguments\n");
        exit(1);
    }

    if (load_so(&handle, syms, argv[1]) < 0) {
        fprintf(stderr, "load so file failed\n");
        exit(1);
    }

    if (dll.handle_init) {
        dll.handle_init("handle_init", 0);
    }

    dll.handle_task("handle_task");

    if (dll.handle_fini) {
        dll.handle_fini("handle_init", 0);
    }

    unload_so(&handle);
    exit(0);
}
#endif /* DLL_TEST */
