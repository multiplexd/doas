#define _PW_NAME_LEN  31
#define UID_MAX UINT_MAX
#define GID_MAX UINT_MAX

int pledge (const char *promises, const char *paths[]);

int shadowauth (const char *u, const char *p);
