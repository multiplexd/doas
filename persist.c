/*
 * Copyright (c) 2018 multiplexd <multi@in-addr.xyz>
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
    pid_t ppid, sid, leader;
    int fd, r, ttynr;
    unsigned long long starttime;

    /* Get the pid of the session leader on our controlling tty */

    if ((fd = open("/dev/tty", O_RDONLY)) == -1)
        return -1;

    r = ioctl(fd, TIOCGSID, &leader);
    close(fd);
    if (r == -1) return -1;

    /* Find our tty number and the start time of our tty's session
       leader. This is a little hairy. */
    if (snprintf(path, sizeof(path), "/proc/%u/stat", leader) >= sizeof(path))
        return -1;

    if ((fd = open(path, O_RDONLY)) == -1)
        return -1;

    p = buf;
    while ((r = read(fd, p, sizeof(buf) - (p - buf))) != 0) {
        if (r == -1) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            break;
        }
        p += r;
    }
    close(fd);

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
    if ((sid = getsid(0)) == -1) return -1;

    if (snprintf(name, namelen, "%d_%d_%d_%llu_%d_%d",
                 getuid(), ttynr, leader, starttime, ppid, sid) >= namelen) {
        return -1;
    }

    return 0;
}

/* Return values: -1 on error, 0 on successful auth file access with
   valid token, 1 on successful auth file access with invalid auth
   token. */
int persist_check(int *authfd) {
    const char *state_dir = DOAS_STATE_DIR;
    struct stat nodeinfo;
    int dirfd, fd;
    char tsname[PATH_MAX];
    struct timespec now;

    /* First, attempt to open the state directory and ensure the permissions
       are valid. */

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

    /* Now construct the name of the timestamp file  */
    if (gettsfilename(tsname, sizeof(tsname)) == -1) {
        close(dirfd);
        return -1;
    }

    /* If the token file doesn't exist then create it */
    fd = openat(dirfd, tsname, O_RDWR | O_SYNC | O_NOFOLLOW);
    if (fd == -1 && errno == ENOENT) {
        fd = openat(dirfd, tsname, O_RDWR | O_CREAT | O_SYNC, 0600);

        if (fd == -1) {
            close(dirfd);
            return -1;
        }

        *authfd = fd;

        /* If we had to create the token file then there's no
           pre-existing auth token that can be valid */
        close(dirfd);
        return 1;
    }

    /* We are now finished with the fd pointing to the state directory */
    close(dirfd);

    /* Now check the permissions of the token file */
    if (fstat(fd, &nodeinfo) == -1) {
        close(fd);
        return -1;
    }

    if (nodeinfo.st_uid != 0 || nodeinfo.st_gid != 0 ||
        nodeinfo.st_mode != (S_IRUSR | S_IWUSR | S_IFREG)) {
        close(fd);
        return -1;
    }

    /* This is a Linuxism. On Linux, CLOCK_MONOTONIC does not run while
       the machine is suspended. */
    if (clock_gettime(CLOCK_BOOTTIME, &now) == -1) {
        close(fd);
        return -1;
    }

    *authfd = fd;

    if (now.tv_sec < nodeinfo.st_mtim.tv_sec)
        /* timestamp is in future, and is thus invalid */
        return 1;

    if ((now.tv_sec - nodeinfo.st_mtim.tv_sec) > DOAS_PERSIST_TIMEOUT)
        /* difference between now and the timestamp is greater than the
           configured timeout */
        return 1;

    /* timestamp is within the timeout */
    return 0;
}

void persist_update(int authfd) {
    struct timespec spec[2];

    /* Only update the last modification time */
    spec[0].tv_nsec = UTIME_OMIT;

    /* This is a Linuxism. See above. */
    if (clock_gettime(CLOCK_BOOTTIME, &spec[1]) == -1)
        return;

    (void) futimens(authfd, spec);
    return;
}

int persist_remove() {
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

