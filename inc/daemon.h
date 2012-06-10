#ifndef __DAEMON_H_INCLUDED__
#define __DAEMON_H_INCLUDED__

#include <stdarg.h>
extern char **daemon_argv_dup(int argc, char *argv[]);
extern void daemon_argv_free(char **daemon_argv);
extern void daemon_set_title(const char* fmt, ...);

void rlimit_reset();
void daemonize();

#endif /* __DAEMON_H_INCLUDED__ */
