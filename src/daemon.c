#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif /* __linux__ */

#define MAXFDS      50000

/* To change the process title in Linux and Solaris we have to
   set argv[1] to NULL and to copy the title to the same place
   where the argv[0] points to. Howerver, argv[0] may be too 
   small to hold a new title. Fortunately, Linux and Solaris 
   store argv[] and environ[] one after another. So we should
   ensure that is the continuous memory and then we allocate
   the new memory for environ[] and copy it. After this we could
   use the memory starting from argv[0] for our process title.

   The Solaris's standard /bin/ps does not show the changed process
   title. You have to use "/usr/ucb/ps -w" instead. Besides, the 
   USB ps does not show a new title if its length less than the 
   origin command line length. To avoid it we append to a new title
   the origin command line in the parenthesis. */

extern char **environ;
static char *arg_start;
static char *arg_end;
static char *env_start;

void daemon_argv_free(char **daemon_argv) {
    int i = 0;
    for (i = 0; daemon_argv[i]; ++i) {
        free(daemon_argv[i]);
    }
    free(daemon_argv);
}

char **daemon_argv_dup(int argc, char *argv[]) {
    arg_start = argv[0];
    arg_end = argv[argc - 1] + strlen(argv[argc - 1]) + 1;
    env_start = environ[0];
    char **saved_argv = (char **)malloc((argc + 1) * sizeof(char *));
    saved_argv[argc] = NULL;
    if (!saved_argv) {
        return NULL;
    }

    while (--argc >= 0) {
        saved_argv[argc] = strdup(argv[argc]);
        if (!saved_argv[argc]) {
            daemon_argv_free(saved_argv);
            return NULL;
        }
    }
    return saved_argv;
}

/* Before invoke this function, you should call daemon_argv_dup first. */
void daemon_set_title(const char* fmt, ...) {
	char title[128];
	int i, tlen;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(title, sizeof(title) - 1, fmt, ap);
	va_end(ap);

	tlen = strlen(title) + 1;
	if (arg_end - arg_start < tlen && env_start == arg_end) {
        /* Need to duplicate environ */
		char *env_end = env_start;
        /* environ is a array of char pointer. 
           Just copy environ strings */
        for (i = 0; environ[i]; ++i) {
            env_end = environ[i] + strlen(environ[i]) + 1;
            environ[i] = strdup(environ[i]);
        }
		arg_end = env_end;
	}

    i = arg_end - arg_start;
    memset(arg_start, 0, i);
    strncpy(arg_start, title, i - 1);
#ifdef __linux__
    prctl(PR_SET_NAME, title);
#endif /* __linux__ */
}

void redirect_std() {
    int fd;
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) {
            close(fd);
        }
    }
}

/* When no 'daemon(3)' supported, use this one. */
void daemonize(int nochdir, int noclose) {
    if (fork() != 0) exit(0); /* parent exits */
    if (!nochdir)  {
        if (chdir("/") != 0) {
            fprintf(stderr, "chdir failed:%s", strerror(errno));
            exit(1);
        }
    }

    if (!noclose) {
        redirect_std();
    }

    setsid(); /* Create a new session. */
}

void rlimit_reset() {
    struct rlimit rlim;
    /* Raise open files */
    rlim.rlim_cur = MAXFDS;
    rlim.rlim_max = MAXFDS;
    setrlimit(RLIMIT_NOFILE, &rlim);

    /* Alow core dump */
    rlim.rlim_cur = 1 << 29;
    rlim.rlim_max = 1 << 29;
    setrlimit(RLIMIT_CORE, &rlim);
}

static int pid_file_lock(int fd, int enable) {
    struct flock f;
    memset(&f, 0, sizeof(f));
    f.l_type = enable ? F_WRLCK : F_UNLCK;
    f.l_whence = SEEK_SET;
    f.l_start = 0;
    f.l_len = 0;

    if (fcntl(fd, F_SETLKW, &f) < 0) {
        if (enable && errno == EBADF) {
            f.l_type = F_RDLCK;
            if (fcntl(fd, F_SETLKW, &f) >= 0) {
                return 0;
            }
        }

        fprintf(stderr, "fcntl(F_SETLKW) failed:%s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int pid_file_create(char *pid_file) {
    int fd, locked, len, ret = -1;
    char buf[16];
    mode_t mode;

    mode = umask(022);

    if ((fd = open(pid_file, O_CREAT | O_RDWR | O_EXCL, 0644)) < 0) {
        goto finish;
    }

    if ((locked = pid_file_lock(fd, 1)) < 0) {
        int saved_errno = errno;
        unlink(pid_file);
        errno = saved_errno;
        goto finish;
    }

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)getpid());
    len = strlen(buf);
    if (write(fd, buf, len) != len) {
        int saved_errno = errno;
        unlink(pid_file);
        errno = saved_errno;
        goto finish;
    }

    ret = 0;

finish:
    if (fd >= 0) {
        int saved_errno = errno;
        if (locked >= 0) {
            pid_file_lock(fd, 0);
        }
        close(fd);
        errno = saved_errno;
    }

    umask(mode);
    return ret;
}

pid_t pid_file_running(char *pid_file) {
    int fd, locked, len;
    char buf[16];
    pid_t pid = (pid_t)-1;

    if ((fd = open(pid_file, O_RDONLY, 0644)) < 0) {
        pid = 0;
        goto finish;
    }

    if ((locked = pid_file_lock(fd, 1)) < 0) {
        goto finish;
    }

    if ((len = read(fd, buf, sizeof(buf) - 1)) < 0) {
        goto finish;
    }

    buf[len] = '\0';
    while (len--) {
        if (buf[len] == '\n' || buf[len] == '\r') {
            buf[len] = '\0';
        }
    }

    pid = (pid_t)strtol(buf, NULL, 10);
    if (pid == 0) {
        fprintf(stderr, "PID file [%s] corrupted, removing\n", pid_file);
        unlink(pid_file);
        errno = EINVAL;
        goto finish;
    }

    if (kill(pid, 0) != 0 && errno != EPERM) {
        int saved_errno = errno;
        unlink(pid_file);
        errno = saved_errno;
        pid = 0;
        goto finish;
    }

finish:
    if (fd >= 0) {
        int saved_errno = errno;
        if (locked >= 0) {
            pid_file_lock(fd, 0);
        }
        close(fd);
        errno = saved_errno;
    }
    return pid;
}

#ifdef DAEMON_TEST_MAIN
int main(int argc, char *argv[]) {
    char **saved_argv = daemon_argv_dup(argc, argv);
    int i = 10;
    if (!saved_argv) {
        fprintf(stderr, "daemon_argv_dup failed\n");
        exit(1);
    }

    daemon_set_title("%s:%d", "It's a very long process title,"
        "please contact with flygoast@126.com", 3);
    while (i-- > 0) {
        sleep(1);
    }
    daemon_argv_free(saved_argv);
}
#endif /* DAEMON_TEST_MAIN */
