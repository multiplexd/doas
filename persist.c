
#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bsd-compat/compat.h"
#include "persist.h"

#define DOAS_STATE_DIR "/var/lib/doas"

int make_auth_file_path(char *path, char *myname, char *tty) {
   const char *state_dir = DOAS_STATE_DIR;
   char *slash = NULL;
   char *tty_clobbered = NULL;
   char *pathtmp = NULL;

   /* Copy the local path of the tty name minus the leading slash */
   tty_clobbered = malloc(sizeof(tty) - 1);
   if (tty_clobbered == NULL) {
      return -1;
   }

   if (strlcpy(tty_clobbered, tty + 1, sizeof(tty_clobbered)) >= sizeof(tty_clobbered)) {
      free(tty_clobbered);
      return -1;
   }

   /* Replace slashes in the tty name with underscores so we can store
      auth files in the form 'user@tty'; for example, for user joe on
      /dev/tty3 the auth file will be (by default)
      /var/lib/doas/joe@dev_tty3. */
   while ((slash = strchr(tty_clobbered, '/')) != NULL) {
      *slash = '_';
   }

   if (asprintf(&pathtmp, "%s/%s@%s", state_dir, myname, tty_clobbered) == -1) {
      free(tty_clobbered);
      return -1;
   }

   if (strlen(pathtmp) > PATH_MAX) {
      free(pathtmp);
      free(tty_clobbered);
      return -1;
   }

   /* Discard the return value as we know we are copying less than or
      equal to PATH_MAX bytes */
   (void) strlcpy(path, pathtmp, sizeof(path));

   free(pathtmp);
   free(tty_clobbered);
   
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

   if (dirinfo.st_uid != 0 || dirinfo.st_gid != 0 || dirinfo.st_mode != 0700) {
      return -1;
   }

   return 0;
}

/* Return values: -1 on error, 0 on successful auth file access with
   valid token, 1 on successful auth file access with invalid auth 
   token */
int persist_check(char *myname, int *authfd) {
   const char *state_dir = DOAS_STATE_DIR;
   struct stat fileinfo;
   char *tty = NULL;
   char token_file[PATH_MAX];
   int fd;
   time_t now;
   
   if (check_dir(state_dir) == -1) {
      return -1;
   }

   if ((tty = ttyname(STDIN_FILENO)) == NULL) {
      return -1;
   }

   if (make_auth_file_path(token_file, myname, tty) == -1) {
      return -1;
   }

   /* If the auth file doesn't exist then create it */
   fd = open(token_file, O_RDWR);
   if (fd == -1 && errno == ENOENT) {
      fd = open(token_file, O_RDWR | O_CREAT, 0600);

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
   if (fileinfo.st_uid != 0 || fileinfo.st_gid != 0 || fileinfo.st_mode != 0600) {
      close(fd);
      return -1;
   }
   
   now = time(NULL);
   if (now = -1) {
      close(fd);
      return -1;
   }

   *authfd = fd;
   
   /* Check if the auth token is valid */
   if (now - fileinfo.st_mtim.tv_sec <= (time_t) 300) {
      return 0;
   } else {
      return 1;
   }
}

/* Force an update of the file's mtime */
void persist_update(int authfd) {
   ftruncate(authfd, 1);
   ftruncate(authfd, 0);
}

int persist_remove(char *myname) {
   const char *state_dir = DOAS_STATE_DIR;
   char *tty = NULL;
   char token_file[PATH_MAX];
   int ret;
   
   if ((tty = ttyname(STDIN_FILENO)) == NULL) {
      return -1;
   }

   if (make_auth_file_path(token_file, myname, tty) == -1) {
      return -1;
   }

   ret = access(token_file, F_OK);
   if (ret == -1 && errno == ENOENT) {
      return 0;
   }
   
   return unlink(token_file);
}
