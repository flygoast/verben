#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include "notifier.h"
#include "log.h"

static char buffer[1024];
static int pipe_fds[2];

static int fd_nonblock(int fd) {
    int flags;
    /* Set the fd nonblocking. Note that fcntl(2) for F_GETFL
       and F_SETFL can't be interrupted by a signal. */
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }
    return 0;
}

int notifier_create() {
    if (pipe(pipe_fds) < 0) {
        fprintf(stderr, "%s\n", strerror(errno));
        return -1;
    }

    assert(fd_nonblock(pipe_fds[0]) == 0);
    assert(fd_nonblock(pipe_fds[1]) == 0);
    fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
}

void notifier_close_wr() {
    close(pipe_fds[1]);
}

void notifier_close_rd() {
    close(pipe_fds[0]);
}

int notifier_read_fd() {
    return pipe_fds[0];
}

int notifier_read() {
    return read(pipe_fds[0], buffer, sizeof(buffer));
}

int notifier_write() {
    char c = 'x';
    return write(pipe_fds[1], &c, 1);
}
