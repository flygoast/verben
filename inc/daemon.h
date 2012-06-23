#ifndef __DAEMON_H_INCLUDED__
#define __DAEMON_H_INCLUDED__

#include <stdarg.h>
#include <unistd.h>

extern char **daemon_argv_dup(int argc, char *argv[]);
extern void daemon_argv_free(char **daemon_argv);
extern void daemon_set_title(const char* fmt, ...);

void rlimit_reset();
void redirect_std();
void daemonize();

pid_t pid_file_running(char *pid_file);
int pid_file_create(char *pid_file);

#endif /* __DAEMON_H_INCLUDED__ */
