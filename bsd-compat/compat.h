/* this file is a mess of declarations to make the BSD compat code work */

#ifndef _BSD_COMPAT_H
#define _BSD_COMPAT_H

/* definitions taken from grepping around /usr/include on an amd64
   OpenBSD 6.1 machine */
#define _PW_NAME_LEN  31
#define UID_MAX UINT_MAX
#define GID_MAX UINT_MAX

/* definition for login.conf file path (empty on non-BSD systems) */
#define _PATH_LOGIN_CONF ""

/* readpassphrase has its own declarations*/
#include "readpassphrase.h"

/* for setprogname */
extern const char *__progname;

long long strtonum(const char *, long long, long long, const char **);
size_t strlcpy(char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
int pledge(const char *, const char *);
int unveil(const char *, const char *);
void *reallocarray(void *, size_t, size_t);
void errc(int, int, const char *, ...);
void closefrom(int);
void setprogname(const char *);

#endif /* _BSD_COMPAT_H */
