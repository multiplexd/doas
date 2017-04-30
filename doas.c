/* $OpenBSD: doas.c,v 1.71 2017/04/06 21:14:12 tedu Exp $ */
/*
 * Copyright (c) 2015 Ted Unangst <tedu@openbsd.org>
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

/* Adapted from the OpenBSD original for use on Linux systems by
   multiplex'd <multiplexd@gmx.com> */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>

#include "bsd-compat/compat.h"
#include "shadowauth.h"
#include "persist.h"
#include "doas.h"

void
usage(void)
{
	fprintf(stderr, "usage: doas [-Lns] [-a style] [-C config] [-u user]"
	    " command [args]\n");
	exit(1);
}

static int
parseuid(const char *s, uid_t *uid)
{
	struct passwd *pw;
	const char *errstr;

	if ((pw = getpwnam(s)) != NULL) {
		*uid = pw->pw_uid;
		return 0;
	}
	*uid = strtonum(s, 0, UID_MAX, &errstr);
	if (errstr)
		return -1;
	return 0;
}

static int
uidcheck(const char *s, uid_t desired)
{
	uid_t uid;

	if (parseuid(s, &uid) != 0)
		return -1;
	if (uid != desired)
		return -1;
	return 0;
}

static int
parsegid(const char *s, gid_t *gid)
{
	struct group *gr;
	const char *errstr;

	if ((gr = getgrnam(s)) != NULL) {
		*gid = gr->gr_gid;
		return 0;
	}
	*gid = strtonum(s, 0, GID_MAX, &errstr);
	if (errstr)
		return -1;
	return 0;
}

static int
match(uid_t uid, gid_t *groups, int ngroups, uid_t target, const char *cmd,
    const char **cmdargs, struct rule *r)
{
	int i;

	if (r->ident[0] == ':') {
		gid_t rgid;
		if (parsegid(r->ident + 1, &rgid) == -1)
			return 0;
		for (i = 0; i < ngroups; i++) {
			if (rgid == groups[i])
				break;
		}
		if (i == ngroups)
			return 0;
	} else {
		if (uidcheck(r->ident, uid) != 0)
			return 0;
	}
	if (r->target && uidcheck(r->target, target) != 0)
		return 0;
	if (r->cmd) {
		if (strcmp(r->cmd, cmd))
			return 0;
		if (r->cmdargs) {
			/* if arguments were given, they should match explicitly */
			for (i = 0; r->cmdargs[i]; i++) {
				if (!cmdargs[i])
					return 0;
				if (strcmp(r->cmdargs[i], cmdargs[i]))
					return 0;
			}
			if (cmdargs[i])
				return 0;
		}
	}
	return 1;
}

static int
permit(uid_t uid, gid_t *groups, int ngroups, const struct rule **lastr,
    uid_t target, const char *cmd, const char **cmdargs)
{
	int i;

	*lastr = NULL;
	for (i = 0; i < nrules; i++) {
		if (match(uid, groups, ngroups, target, cmd,
		    cmdargs, rules[i]))
			*lastr = rules[i];
	}
	if (!*lastr)
		return 0;
	return (*lastr)->action == PERMIT;
}

static void
parseconfig(const char *filename, int checkperms)
{
	extern FILE *yyfp;
	extern int yyparse(void);
	struct stat sb;

	yyfp = fopen(filename, "r");
	if (!yyfp)
		err(1, checkperms ? "doas is not enabled, %s" :
		    "could not open config file %s", filename);

	if (checkperms) {
		if (fstat(fileno(yyfp), &sb) != 0)
			err(1, "fstat(\"%s\")", filename);
		if ((sb.st_mode & (S_IWGRP|S_IWOTH)) != 0)
			errx(1, "%s is writable by group or other", filename);
		if (sb.st_uid != 0)
			errx(1, "%s is not owned by root", filename);
	}

	yyparse();
	fclose(yyfp);
	if (parse_errors)
		exit(1);
}

void
checkconfig(const char *confpath, int argc, char **argv,
    uid_t uid, gid_t *groups, int ngroups, uid_t target)
{
	const struct rule *rule;

	setresuid(uid, uid, uid);
	parseconfig(confpath, 0);
	if (!argc)
		exit(0);

	if (permit(uid, groups, ngroups, &rule, target, argv[0],
	    (const char **)argv + 1)) {
		printf("permit%s\n", (rule->options & NOPASS) ? " nopass" : "");
		exit(0);
	} else {
		printf("deny\n");
		exit(1);
	}
}

static void
authuser(char *myname, char *login_style, int persist)
{
	char *challenge = NULL, *response, rbuf[1024], cbuf[128];
	char host[HOST_NAME_MAX + 1];
	char path[PATH_MAX]; /* In case tokens need to be deleted due to failed auth */
	int ttyfd = -1;
	int authfd = -1;
	int ret = -1;

	if (persist)
		ttyfd = open("/dev/tty", O_RDWR);
	if (ttyfd != -1) {
	        ret = persist_check(myname, &authfd, path);
		if(ret == 0) 
		       goto good;
	}

	if (gethostname(host, sizeof(host)))
   	        snprintf(host, sizeof(host), "?");
	snprintf(cbuf, sizeof(cbuf),
		 "\rdoas (%.32s@%.32s) password: ", myname, host);
	challenge = cbuf;

	response = readpassphrase(challenge, rbuf, sizeof(rbuf),
	    RPP_REQUIRE_TTY);
	if (response == NULL && errno == ENOTTY) {
		syslog(LOG_AUTHPRIV | LOG_NOTICE,
		    "tty required for %s", myname);
		if (ret == 1)
		        unlink(path); 
		errx(1, "a tty is required");
	}

	if(shadowauth(myname, response) != 0) {
		syslog(LOG_AUTHPRIV | LOG_NOTICE,
		    "failed auth for %s", myname);
		if (ret == 1)
		        unlink(path);
		errc(1, EPERM, NULL);
	}
#ifndef NO_EXPLICIT_BZERO
	explicit_bzero(rbuf, sizeof(rbuf));
#else
	memset(rbuf, 0, sizeof(rbuf));
#endif
good:
	if (ttyfd != -1 && ret != -1) {
	        persist_update(authfd);
		close(authfd);
		close(ttyfd);
        } 
}

int
main(int argc, char **argv)
{
	const char *safepath = "/bin:/sbin:/usr/bin:/usr/sbin:"
	    "/usr/local/bin:/usr/local/sbin";
	const char *confpath = NULL;
	char *shargv[] = { NULL, NULL };
	char *sh;
	const char *cmd;
	char cmdline[LINE_MAX];
	char myname[_PW_NAME_LEN + 1];
	struct passwd *pw;
	const struct rule *rule;
	uid_t uid;
	uid_t target = 0;
	gid_t groups[NGROUPS_MAX + 1];
	int ngroups;
	int i, ch;
	int sflag = 0;
	int nflag = 0;
	char cwdpath[PATH_MAX];
	const char *cwd;
	char *login_style = NULL;
	char **envp;

	setprogname("doas");

	closefrom(STDERR_FILENO + 1);

	uid = getuid();

	/* Need to find out the name of the calling user before option
	   processing takes place in case we are resetting auth
	   tokens. */
	pw = getpwuid(uid);
	if (!pw)
		err(1, "getpwuid failed");
	if (strlcpy(myname, pw->pw_name, sizeof(myname)) >= sizeof(myname))
		errx(1, "pw_name too long");

	if (geteuid())
		errx(1, "not installed setuid");

	while ((ch = getopt(argc, argv, "a:C:Lnsu:")) != -1) {
		switch (ch) {
		case 'a':
			login_style = optarg;
			break;
		case 'C':
			confpath = optarg;
			break;
		case 'L':
			i = open("/dev/tty", O_RDWR);
			if (i != -1) 
				i = persist_remove(myname);
			if (i == -1)
			        errx(1, "could not clear auth token");
			exit(0); 
		case 'u':
			if (parseuid(optarg, &target) != 0)
				errx(1, "unknown user");
			break;
		case 'n':
			nflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		default:
			usage();
			break;
		}
	}
	argv += optind;
	argc -= optind;

	if (confpath) {
		if (sflag)
			usage();
	} else if ((!sflag && !argc) || (sflag && argc))
		usage();

	ngroups = getgroups(NGROUPS_MAX, groups);
	if (ngroups == -1)
		err(1, "can't get groups");
	groups[ngroups++] = getgid();

	if (sflag) {
		sh = getenv("SHELL");
		if (sh == NULL || *sh == '\0') {
			shargv[0] = strdup(pw->pw_shell);
			if (shargv[0] == NULL)
				err(1, NULL);
		} else
			shargv[0] = sh;
		argv = shargv;
		argc = 1;
	}

	if (confpath) {
		checkconfig(confpath, argc, argv, uid, groups, ngroups,
		    target);
		exit(1);	/* fail safe */
	}

	parseconfig("/etc/doas.conf", 1);

	/* cmdline is used only for logging, no need to abort on truncate */
	(void)strlcpy(cmdline, argv[0], sizeof(cmdline));
	for (i = 1; i < argc; i++) {
		if (strlcat(cmdline, " ", sizeof(cmdline)) >= sizeof(cmdline))
			break;
		if (strlcat(cmdline, argv[i], sizeof(cmdline)) >= sizeof(cmdline))
			break;
	}

	openlog(__progname, LOG_PID, LOG_AUTHPRIV | LOG_NOTICE);
	
	cmd = argv[0];
	if (!permit(uid, groups, ngroups, &rule, target, cmd,
	    (const char **)argv + 1)) {
		syslog(LOG_AUTHPRIV | LOG_NOTICE,
		    "failed command for %s: %s", myname, cmdline);
		errc(1, EPERM, NULL);
	}

	if (!(rule->options & NOPASS)) {
		if (nflag)
			errx(1, "Authorization required");

		authuser(myname, login_style, rule->options & PERSIST);
	}

	if (pledge("stdio rpath getpw exec id", NULL) == -1)
		err(1, "pledge");

	pw = getpwuid(target);
	if (!pw)
		errx(1, "no passwd entry for target");

	/* do the heavy lifting otherwise done by setusercontext() manually */
	if (initgroups(pw->pw_name, pw->pw_gid) == -1)
	        err(1, "failed to set supplementary groups for '%s'", pw->pw_name);
	if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1)
	        err(1, "failed to change to gid of '%s'", pw->pw_name);
	if (setresuid(target, target, target) == -1)
	        err(1, "failed to change to uid of '%s'", pw->pw_name);

	if (pledge("stdio rpath exec", NULL) == -1)
		err(1, "pledge");

	if (getcwd(cwdpath, sizeof(cwdpath)) == NULL)
		cwd = "(failed)";
	else
		cwd = cwdpath;

	if (pledge("stdio exec", NULL) == -1)
		err(1, "pledge");

	syslog(LOG_AUTHPRIV | LOG_INFO, "%s ran command %s as %s from %s",
	    myname, cmdline, pw->pw_name, cwd);

	envp = prepenv(rule);

	if (rule->cmd) {
		if (setenv("PATH", safepath, 1) == -1)
			err(1, "failed to set PATH '%s'", safepath);
	}

	execvpe(cmd, argv, envp);
	if (errno == ENOENT)
		errx(1, "%s: command not found", cmd);
	err(1, "%s", cmd);
}
