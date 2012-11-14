/* This plugin is used to turn verben into a lua application server. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include "verben.h"
#include "version.h"
#include "plugin.h"
#include "log.h"
#include "conf.h"

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

static lua_State *globalL = NULL;

void __vlua_plugin_main(void) {
    printf("** verben [vlua] plugin **\n");
    printf("Copyright(c)flygoast, flygoast@126.com\n");
    printf("verben version: %s\n", VERBEN_VERSION);
    exit(0);
}

int handle_init(void *cycle, int proc_type) {
    conf_t *conf = (conf_t*)cycle;

    globalL = luaL_newstate();
    luaL_openlibs(globalL);


    /* Never get here. */
    return -1;
}

/* This API implementation is optional. */
int handle_open(char **sendbuf, int *len, 
        const char *remote_ip, int port) {
}


void handle_close(const char *remote_ip, int port) {
}

int handle_input(char *buf, int len, const char *remote_ip, int port) {
}

int handle_process(char *rcvbuf, int rcvlen, 
        char **sndbuf, int *sndlen, const char *remote_ip, int port) {
}

/* This function used to free the memory allocated in handle_process().
 * It is NOT mandatory. */
void handle_process_post(char *sendbuf, int sendlen) {
}

void handle_fini(void *cycle, int proc_type) {
}
