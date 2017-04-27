
#ifndef _BSD_COMPAT_H
#define _BSD_COMPAT_H

#define _PW_NAME_LEN  31
#define UID_MAX UINT_MAX
#define GID_MAX UINT_MAX

#include "readpassphrase.h"

extern const char *__progname;

long long strtonum(const char *, long long, long long, const char **);
size_t strlcpy(char *dst, const char *src, size_t siz);
size_t strlcat(char *dst, const char *src, size_t siz);
int pledge (const char *promises, const char *paths[]);
void *reallocarray(void *, size_t, size_t);
void errc(int, int, const char *, ...);
void closefrom(int);
void setprogname(const char *);

#endif /* _BSD_COMPAT_H */
