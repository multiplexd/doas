/*
 * Copyright (c) 2018 multi <multi@in-addr.xyz>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Thanks to Duncan Overbruck for pointing out various issues with the
 * previous versions of this code and with suggestions for alternative
 * example code for comparison.
 */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "bsd-compat/compat.h"
#include "persist.h"

#ifndef DOAS_STATE_DIR
#define DOAS_STATE_DIR "/var/lib/doas"
#endif

#ifndef DOAS_PERSIST_TIMEOUT
#define DOAS_PERSIST_TIMEOUT 300 /* Five minutes */
#endif


/* Credit for this function goes to Duncan Overbruck. Flameage for this
   function goes to multiplexd. */
static int gettsfilename(char *name, size_t namelen) {
    char buf[1024], path[PATH_MAX], *p, *ep;
    const char *errstr;
    pid_t ppid, sid;
    int fd, r, ttynr;
    unsigned long long starttime;

    /* Find our session leader, the number of their controlling tty, and their
       start time. */
    if ((sid = getsid(0)) == -1)
        return -1;

    if (snprintf(path, sizeof(path), "/proc/%u/stat", sid) >= sizeof(path))
        return -1;

    if ((fd = open(path, O_RDONLY)) == -1)
        return -1;

    p = buf;
    while ((r = read(fd, p, buf + (sizeof(buf) - 1) - p)) != 0) {
        if (r == -1) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            close(fd);
            return -1;
        }
        p += r;
    }
    close(fd);

    /* Discard the buffer if it contains NUL chars, as we can't safely parse it
       in that case */
    if (memchr(buf, '\0', p - buf - 1) != NULL)
        return -1;

    *p = '\0';

    /* We parse from the end of the second field by spaces, as the
       second field can itself contain spaces */
    if ((p = strrchr(buf, ')')) == NULL)
        return -1;

    /* We want the seventh and twenty-second fields; see proc(5) */
    r = 2;
    for ((p = strtok(p, " ")); p; (p = strtok(NULL, " "))) {
        switch (r++) {
        case 7:
            ttynr = strtonum(p, INT_MIN, INT_MAX, &errstr);
            if (errstr)
                return -1;
            break;
        case 22:
            errno = 0;
            starttime = strtoull(p, &ep, 10);
            if (p == ep || (errno == ERANGE && starttime == ULLONG_MAX))
                return -1;
            break;
        }
        if (r == 23)
            break;
    }

    ppid = getppid();
    if (snprintf(name, namelen, "%d_%d_%d_%llu_%d",
                 getuid(), ttynr, sid, starttime, ppid) >= namelen) {
        return -1;
    }

    return 0;
}

/* Assumes the process has a controlling tty. */
int persist_check(int *authfd, int *dirfd, char *name, size_t namesz, char *tmp, size_t tmpsz) {
    const char *state_dir = DOAS_STATE_DIR;
    int fd, sfd;
    struct stat nodeinfo;
    struct timespec now;

    /* Open state directory and verify permissions */
    if ((sfd = open(state_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) == -1)
        return PERSIST_ERROR;

    if (fstat(sfd, &nodeinfo) == -1)
        goto closedir;

    if (nodeinfo.st_uid != 0 || nodeinfo.st_gid != 0 ||
        nodeinfo.st_mode != (S_IRWXU | S_IFDIR))
        goto closedir;

    /* Get the name of the timestamp file */
    if (gettsfilename(name, namesz) == -1)
        goto closedir;

    fd = openat(sfd, name, O_RDWR | O_SYNC | O_NOFOLLOW);
    if (fd == -1) {
        if (errno == ENOENT) {
            /* Timestamp file doesn't exist, so create temporary file to be
               renamed if authentication succeeds */

            if (snprintf(tmp, tmpsz, "tmp.%s.%d", name, getpid()) >= tmpsz)
                goto closedir;

            fd = openat(sfd, tmp, O_RDWR | O_CREAT | O_SYNC, 0600);
            if (fd == -1)
                goto closedir;

            *authfd = fd;
            *dirfd = sfd;
            return PERSIST_NEW;
        } else
            goto closedir;
    }

    /* Now finished with state directory */
    close(sfd);

    /* Check permissions of token file */
    if (fstat(fd, &nodeinfo) == -1)
        goto closefd;

    if (nodeinfo.st_uid != 0 || nodeinfo.st_gid != 0 ||
        nodeinfo.st_mode != (S_IRUSR | S_IWUSR | S_IFREG))
        goto closefd;

    /* This is a Linuxism. On Linux, CLOCK_MONOTONIC does not run while
       the machine is suspended. */
    if (clock_gettime(CLOCK_BOOTTIME, &now) == -1)
        goto closefd;

    *authfd = fd;

    if (now.tv_sec < nodeinfo.st_mtim.tv_sec)
        /* Timestamp is in future, and is thus invalid */
        return PERSIST_INVALID;

    if ((now.tv_sec - nodeinfo.st_mtim.tv_sec) > DOAS_PERSIST_TIMEOUT)
        /* Difference between now and the timestamp is greater than the
           configured timeout */
        return PERSIST_INVALID;

    /* Timestamp is within the timeout */
    return PERSIST_OK;

closefd:
    close(fd);
    return PERSIST_ERROR;

closedir:
    close(sfd);
    return PERSIST_ERROR;
}

void persist_update(int fd) {
    struct timespec spec[2];

    /* Only update the last modification time */
    spec[0].tv_nsec = UTIME_OMIT;

    /* This is a Linuxism. See above. */
    if (clock_gettime(CLOCK_BOOTTIME, &spec[1]) == -1)
        return;

    (void) futimens(fd, spec);
    close(fd);
}

void persist_commit(int dirfd, char *from, char *to) {
    (void) renameat(dirfd, from, dirfd, to);
    close(dirfd);
}

int persist_clear() {
    const char *state_dir = DOAS_STATE_DIR;
    struct stat nodeinfo;
    char tsname[PATH_MAX];
    int dirfd;
    int r, e;

    /* Open and check the state directory */

    if ((dirfd = open(state_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW)) == -1)
        return -1;

    if (fstat(dirfd, &nodeinfo) == -1) {
        close(dirfd);
        return -1;
    }

    if (nodeinfo.st_uid != 0 || nodeinfo.st_gid != 0 ||
         nodeinfo.st_mode != (S_IRWXU | S_IFDIR)) {
        close(dirfd);
        return -1;
    }

    if (gettsfilename(tsname, sizeof(tsname)) == -1) {
        close(dirfd);
        return -1;
    }

    r = unlinkat(dirfd, tsname, 0);
    e = errno;
    close(dirfd);

    return (r == 0 || (r == -1 && e == ENOENT)) ? 0 : -1;
}

