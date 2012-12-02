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

#define VERBEN_LIBNAME      "verben"
#define VERBEN_LUA_VERSION  "0.0.1"

static lua_State *globalL = NULL;

typedef struct conf_media {
    lua_State       *L;
    unsigned int    count;
} conf_media_t;

void __vlua_plugin_main(void) {
    printf("** verben [vlua] plugin **\n");
    printf("Copyright(c)Feng Gu, flygoast@126.com\n");
    printf("verben version: %s\n", VERBEN_VERSION);
    printf("verben vlua version: %s\n", VERBEN_LUA_VERSION);
    exit(0);
}

static int v_log(lua_State *L) {
    int i;
    int top;
    int level;
    const char *message;

    level = luaL_checkint(L, 1);
    if (level > LOG_LEVEL_ALL || level < 0) {
        WARNING_LOG("level param from lua log function is invalid: %d", level);
        return 0;
    }

    top = lua_gettop(L);
    for (i = 2; i <= top; ++i) {
        int t = lua_type(L, i);
        switch (t) {
        case LUA_TTABLE:
            lua_pushfstring(L, "(Table: %p)", lua_topointer(L, i));
            lua_replace(L, i);
            break;
        case LUA_TBOOLEAN:
            if (lua_toboolean(L, i)) {
                lua_pushstring(L, "(true)");
            } else {
                lua_pushstring(L, "(false)");
            }
            lua_replace(L, i);
            break;
        case LUA_TNIL:
            lua_pushstring(L, "(nil)");
            lua_replace(L, i);
            break;
        }
    }

    /* concates if there is more than one string parameter */
    lua_concat(L, lua_gettop(L) - 1);

    message = luaL_checkstring(L, 2);
    DETAIL(level, "%s", message);
    return 0;
}

static int push_config(void *key, void *value, void *ptr) {
    conf_media_t *media = (conf_media_t *)ptr;
    lua_pushstring(media->L, (char *)value);
    ++media->count;
    return 0;
}

static int v_config(lua_State *L) {
    unsigned int i;
    conf_t *conf = (conf_t *)lua_touserdata(L, 1);
    char *name = (char *)luaL_checkstring(L, 2);
    conf_media_t media = { L, 0 };

    lua_newtable(L);

    conf_array_foreach(conf, name, push_config, (void *)&media);

    for (i = 1; i <= media.count; ++i) {
        lua_pushinteger(L, i);
        lua_insert(L, -2);
        lua_settable(L, 3);
    }
    return 1;
}

static const luaL_Reg verben_lib[] = {
    { "config",     v_config    },
    { "log",        v_log       },
    { NULL,         NULL        }
};

#define set_const(key, value)   \
    lua_pushliteral(L, key);    \
    lua_pushnumber(L, value);   \
    lua_settable(L, -3)

static void register_verben(lua_State *L) {
    luaL_register(L, VERBEN_LIBNAME, verben_lib);

    lua_pushliteral(L, "VLUA_VERSION");
    lua_pushliteral(L, VERBEN_LUA_VERSION);
    lua_settable(L, -3);

    set_const("VERBEN_OK", VERBEN_OK);
    set_const("VERBEN_ERROR", VERBEN_ERROR);
    set_const("VERBEN_CONN_CLOSE", VERBEN_CONN_CLOSE);

    set_const("FATAL", LOG_LEVEL_FATAL);
    set_const("ERROR", LOG_LEVEL_ERROR);
    set_const("WARNING", LOG_LEVEL_WARNING);
    set_const("NOTICE", LOG_LEVEL_NOTICE);
    set_const("DEBUG", LOG_LEVEL_DEBUG);


    set_const("MASTER", VB_PROCESS_MASTER);
    set_const("WORKER", VB_PROCESS_WORKER);
    set_const("CONN", VB_PROCESS_CONN);

    /* keep stack balance */
    lua_pop(L, 1);
}

static int call_handle_init(lua_State *L, conf_t *conf, int proc_type) {
    lua_getglobal(L, "handle_init");
    lua_pushlightuserdata(L, conf);
    lua_pushinteger(L, proc_type);

    if (lua_pcall(L, 2, 1, 0)) {
        fprintf(stderr, "Call lua handle_init failed: %s\n", 
            lua_tostring(L, -1));
        return -1;
    }

    if (luaL_checkint(L, 1)) {
        fprintf(stderr, "Lua handle_init in process[%d] failed:"
            " returned value: %d\n", proc_type, luaL_checkint(L, 1));
        lua_pop(L, 1);
        return -1;
    }

    lua_pop(L, 1);
    return 0;
}

int handle_init(void *cycle, int proc_type) {
    conf_t *conf = (conf_t*)cycle;
    const char *lua_file;

    if (proc_type == VB_PROCESS_MASTER) {
        lua_file = conf_get_str_value(conf, "luafile", NULL);
        if (lua_file == NULL) {
            fprintf(stderr, "'luafile' configure not found\n");
            return -1;
        }
    
        globalL = luaL_newstate();
        luaL_openlibs(globalL);
    
        register_verben(globalL);
    
        if (lua_gettop(globalL)) {
            fprintf(stderr, "Lua stack is not empty\n");
            return -1;
        }
    
        if (luaL_loadfile(globalL, lua_file)) {
            fprintf(stderr, "Load lua file %s failed\n", lua_file); 
            return -1;
        }
    
        if (lua_pcall(globalL, 0, LUA_MULTRET, 0)) {
            fprintf(stderr, "Prepare the lua file %s failed: %s\n", 
                lua_file, lua_tostring(globalL, -1)); 
            return -1;
        }
    }

    if (lua_gettop(globalL)) {
        fprintf(stderr, "Lua stack is not empty\n");
        return -1;
    }

    return call_handle_init(globalL, conf, proc_type);
}

int handle_open(char **sendbuf, int *len, 
        const char *remote_ip, int port) {
    const char *response;
    char *buf;
    int ret;

    lua_getglobal(globalL, "handle_open");
    lua_newtable(globalL);
    lua_pushstring(globalL, remote_ip);
    lua_setfield(globalL, -2, "remote_ip");
    lua_pushinteger(globalL, port);
    lua_setfield(globalL, -2, "remote_port");

    if (lua_pcall(globalL, 1, 2, 0)) {
        FATAL_LOG("Call lua handle_open failed: %s", 
            lua_tostring(globalL, -1));
        lua_pop(globalL, 1);
        return -1;
    }

    ret = luaL_checkint(globalL, 1);
    response = lua_tolstring(globalL, 2, (size_t *)len);
    
    DEBUG_LOG("Lua handle_open returned: %d, %s, length: %d", ret,
        response ? response : "(NULL)", *len);

    if (!response) {
        *sendbuf = NULL;
        *len = 0;
        return ret;
    }

    buf = (char *)malloc(*len);
    assert(buf);
    memcpy(buf, response, *len);
    *sendbuf = buf;
    lua_pop(globalL, 2);

    return ret;
}

void handle_close(const char *remote_ip, int port) {
    lua_getglobal(globalL, "handle_close");
    lua_newtable(globalL);
    lua_pushstring(globalL, remote_ip);
    lua_setfield(globalL, -2, "remote_ip");
    lua_pushinteger(globalL, port);
    lua_setfield(globalL, -2, "remote_port");

    if (lua_pcall(globalL, 1, 0, 0)) {
        FATAL_LOG("Call lua handle_open failed: %s", 
            lua_tostring(globalL, -1));
        lua_pop(globalL, 1);
    }
}

int handle_input(char *buf, int len, const char *remote_ip, int port) {
    int expect_len;

    lua_getglobal(globalL, "handle_input");
    lua_newtable(globalL);
    lua_pushstring(globalL, remote_ip);
    lua_setfield(globalL, -2, "remote_ip");
    lua_pushinteger(globalL, port);
    lua_setfield(globalL, -2, "remote_port");
    lua_pushlstring(globalL, buf, len);
    lua_setfield(globalL, -2, "content");
    lua_pushinteger(globalL, len);
    lua_setfield(globalL, -2, "length");

    if (lua_pcall(globalL, 1, 1, 0)) {
        FATAL_LOG("Call lua handle_input failed: %s", 
            lua_tostring(globalL, -1));
        lua_pop(globalL, 1);
        return 0;
    }

    expect_len = luaL_checkint(globalL, 1);
    lua_pop(globalL, 1);

    return expect_len;
}

int handle_process(char *rcvbuf, int rcvlen, 
        char **sndbuf, int *sndlen, const char *remote_ip, int port) {
    const char *response;
    char *buf;
    int ret;

    lua_getglobal(globalL, "handle_process");
    lua_newtable(globalL);
    lua_pushstring(globalL, remote_ip);
    lua_setfield(globalL, -2, "remote_ip");
    lua_pushinteger(globalL, port);
    lua_setfield(globalL, -2, "remote_port");
    lua_pushlstring(globalL, rcvbuf, rcvlen);
    lua_setfield(globalL, -2, "content");
    lua_pushinteger(globalL, rcvlen);
    lua_setfield(globalL, -2, "length");

    if (lua_pcall(globalL, 1, 2, 0)) {
        FATAL_LOG("Call lua handle_process failed: %s", 
            lua_tostring(globalL, -1));
        lua_pop(globalL, 1);
        return VERBEN_ERROR;
    }

    ret = luaL_checkint(globalL, 1);
    response = lua_tolstring(globalL, 2, (size_t *)sndlen);

    DEBUG_LOG("Lua handle_process returned: %d, %s", ret, 
        response? response : "(NULL)");

    if (!response) {
        return ret;
    }


    buf = (char *)malloc(*sndlen);
    if (!buf) {
        FATAL_LOG("Out of memory");
        return ret;
    }
    memcpy(buf, response, *sndlen);
    *sndbuf = buf;
    lua_pop(globalL, 2);

    return ret;
}

/* This function used to free the memory allocated in handle_process().
 * It is NOT mandatory. */
void handle_process_post(char *sendbuf, int sendlen) {
    if (sendbuf) {
        free(sendbuf);
    }
}

void handle_fini(void *cycle, int proc_type) {
    conf_t *conf = (conf_t *)cycle;

    lua_getglobal(globalL, "handle_fini");
    lua_pushlightuserdata(globalL, conf);
    lua_pushinteger(globalL, proc_type);

    if (lua_pcall(globalL, 2, 0, 0)) {
        FATAL_LOG("Call lua handle_fini failed: %s\n", 
            lua_tostring(globalL, -1));
        lua_pop(globalL, 1);
    }

    lua_close(globalL);
}
