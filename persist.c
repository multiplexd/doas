/*
 * Copyright (c) 2017 multiplex'd <multiplexd@gmx.com>
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

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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

int make_auth_file_path(char *path, char *myname, char *tty) {
   const char *state_dir = DOAS_STATE_DIR;
   char *slash = NULL;
   char tty_clobbered[PATH_MAX];
   char *pathtmp = NULL;
   pid_t ppid = getppid();

   if (strlcpy(tty_clobbered, tty + 1, sizeof(tty_clobbered)) >= sizeof(tty_clobbered)) {
      return -1;
   }

   /* Replace slashes in the tty name with underscores so we can store
      auth files in the form 'user@tty@ppid'; for example, for user
      joe on /dev/tty3 with shell process id 1285 the auth file will
      be (by default) /var/lib/doas/joe@dev_tty3@1285. */
   while ((slash = strchr(tty_clobbered, '/')) != NULL) {
      *slash = '_';
   }
   
   if (asprintf(&pathtmp, "%s/%s@%s@%d", state_dir, myname, tty_clobbered, ppid) == -1) {
      return -1;
   }

   if (strlen(pathtmp) > PATH_MAX) {
      free(pathtmp);
      return -1;
   }

   if (strlcpy(path, pathtmp, PATH_MAX) >= PATH_MAX) {
      free(pathtmp);
      return -1;
   }

   free(pathtmp);
   
   return 0;
}

int check_dir(const char *dir) {
   struct stat dirinfo;

   if (stat(dir, &dirinfo) < 0) {
      return -1;
   }

   if (! S_ISDIR(dirinfo.st_mode)) {
      return -1;
   }

   if (dirinfo.st_uid != 0 || dirinfo.st_gid != 0 || dirinfo.st_mode != (S_IRWXU | S_IFDIR)) {
      return -1;
   }

   return 0;
}

/* This is (surprise surprise) a hack! Since Linux (nor any other Unix-like platform)
   offers no clean way to resolve /dev/tty to a path which is actually useful (like e.g.
   /dev/tty2), we're going to gamble on one of STD{IN,OUT,ERR} being connected to our
   controlling tty. This will make life interesting if doas is run from a script. sudo's
   way of working out what its controlling tty is (on Linux) involves opening
   /proc/self/stat, finding the device ID number of its controlling terminal and then
   iterating over EVERY DEVICE NODE under /dev until it finds a tty with a matching
   device ID. Oh and there was a CVE against sudo's parsing of /proc/self/stat a few
   months before I wrote this comment. */

/* Working with ttys requires regular doses of brainbleach to maintain sanity */
char * ttyname_hack() {
   int i;
   char *tty;

   for (i = 0; i <=2; i++) {
      if ((tty = ttyname(i)) != NULL)
         return tty;
   }
   
   return NULL;
}

/* Return values: -1 on error, 0 on successful auth file access with
   valid token, 1 on successful auth file access with invalid auth
   token. */
int persist_check(char *myname, int *authfd) {
   const char *state_dir = DOAS_STATE_DIR;
   struct stat fileinfo;
   char *tty = NULL;
   char token_file[PATH_MAX];
   int fd;
   time_t rec, diff;
   struct timespec now;
   ssize_t ret, total;
   
   if (check_dir(state_dir) == -1) {
      return -1;
   }

   tty = ttyname_hack();
   if (tty == NULL || strlen(tty) > PATH_MAX) {
      return -1;
   }

   if (make_auth_file_path(token_file, myname, tty) == -1) {
      return -1;
   }

   /* If the auth file doesn't exist then create it */
   fd = open(token_file, O_RDWR| O_SYNC);
   if (fd == -1 && errno == ENOENT) {
      fd = open(token_file, O_RDWR | O_CREAT | O_SYNC, 0600);

      if (fd == -1) {
	 return -1;
      }

      *authfd = fd;
      
      /* If we had to create the auth file then there's no
         pre-existing auth token that can be valid */
      return 1;
   }

   if (fstat(fd, &fileinfo) == -1) {
      close(fd);
      return -1;
   }

   /* Make sure that the permissions of the auth token file are what
      we expect */
   if (fileinfo.st_uid != 0 || fileinfo.st_gid != 0 ||
       fileinfo.st_mode != (S_IRUSR | S_IWUSR | S_IFREG)) {

      close(fd);
      return -1;
   }

   /* This is a Linuxism. On Linux, CLOCK_MONOTONIC does not run while
      the machine is suspended. */
   if (clock_gettime(CLOCK_BOOTTIME, &now) < 0) {
      close(fd);
      return -1;
   }

   ret = read(fd, (void*) &rec, sizeof(time_t));
   if (ret < 0) {
      /* I/O error, abort */
      close(fd);
      return -1;
   } else if (ret == 0) {
      /* Empty file. We have a token file, but it isn't valid */
      return 1;
   } else if (ret != sizeof(time_t)) {
      /* At this point, either the token file is bad or we haven't been able to
         read a whole time_t, but we can't tell */
      total += ret;
      ret = read(fd, (void*) &rec + (sizeof(time_t) - ret), sizeof(time_t) - ret);
      printf("%d\n", ret);

      if (ret < 0) {
         /* I/O error, abort */
         close(fd);
         return -1;
      } else if (ret == 0) {
         /* End of file. This file is invalid, but it's here. */
         return 1;
      } else if (ret + total != sizeof(time_t)) {
         /* We can't seem to read all the data out of this file, but it's there */
         return 1;
      }
   }

   *authfd = fd;

   /* Make sure that the recorded time is in the past */
   if (now.tv_sec < rec) {
      return 1;
   }
   
   /* Check if the auth token is valid */
   diff = now.tv_sec - rec;
   if(diff < 0 || diff > DOAS_PERSIST_TIMEOUT || !(diff <= DOAS_PERSIST_TIMEOUT)) {
      return 1;
   } 
   return 0;
}

/* Force an update of the file's mtime */
void persist_update(int authfd) {
   struct timespec now;

   /* This is a Linuxism. See above. */
   if (clock_gettime(CLOCK_BOOTTIME, &now) < 0)
      return;

   lseek(authfd, 0, SEEK_SET);
   ftruncate(authfd, 0);
   
   write(authfd, (void*) &now.tv_sec, sizeof(time_t));

   return;
}

int persist_remove(char *myname) {
   const char *state_dir = DOAS_STATE_DIR;
   char *tty = NULL;
   char token_file[PATH_MAX];
   int ret;
   
   if ((tty = ttyname_hack()) == NULL) {
      return -1;
   }

   if (make_auth_file_path(token_file, myname, tty) == -1) {
      return -1;
   }

   /* Perform check using effective uid/gid instead of real uid/gid */
   ret = faccessat(0, token_file, F_OK, AT_EACCESS);
   if (ret == -1 && errno == ENOENT) {
      return 0;
   }
   
   return unlink(token_file);
}
