#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#ifdef I_UNISTD
#   include <unistd.h>
#endif

/* The realpath() implementation from OpenBSD 2.9 (realpath.c 1.4)
 * Renamed here to bsd_realpath() to avoid library conflicts.
 * --jhi 2000-06-20 */

/*
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char *rcsid = "$OpenBSD: realpath.c,v 1.4 1998/05/18 09:55:19 deraadt Exp $";
#endif /* LIBC_SCCS and not lint */

/* OpenBSD system #includes removed since the Perl ones should do. --jhi */

#ifndef MAXSYMLINKS
#define MAXSYMLINKS 8
#endif

/*
 * char *realpath(const char *path, char resolved_path[MAXPATHLEN]);
 *
 * Find the real name of path, by removing all ".", ".." and symlink
 * components.  Returns (resolved) on success, or (NULL) on failure,
 * in which case the path which caused trouble is left in (resolved).
 */
static
char *
bsd_realpath(path, resolved)
	const char *path;
	char *resolved;
{
#ifdef VMS
       dTHX;
       return Perl_rmsexpand(aTHX_ (char*)path, resolved, NULL, 0);
#else
	int rootd, serrno;
	char *p, *q, wbuf[MAXPATHLEN];
	int symlinks = 0;

	/* Save the starting point. */
#ifdef HAS_FCHDIR
	int fd;

	if ((fd = open(".", O_RDONLY)) < 0) {
		(void)strcpy(resolved, ".");
		return (NULL);
	}
#else
	char wd[MAXPATHLEN];

	if (getcwd(wd, MAXPATHLEN - 1) == NULL) {
		(void)strcpy(resolved, ".");
		return (NULL);
	}
#endif

	/*
	 * Find the dirname and basename from the path to be resolved.
	 * Change directory to the dirname component.
	 * lstat the basename part.
	 *     if it is a symlink, read in the value and loop.
	 *     if it is a directory, then change to that directory.
	 * get the current directory name and append the basename.
	 */
	(void)strncpy(resolved, path, MAXPATHLEN - 1);
	resolved[MAXPATHLEN - 1] = '\0';
loop:
	q = strrchr(resolved, '/');
	if (q != NULL) {
		p = q + 1;
		if (q == resolved)
			q = "/";
		else {
			do {
				--q;
			} while (q > resolved && *q == '/');
			q[1] = '\0';
			q = resolved;
		}
		if (chdir(q) < 0)
			goto err1;
	} else
		p = resolved;

#if defined(HAS_LSTAT) && defined(HAS_READLINK) && defined(HAS_SYMLINK)
    {
	struct stat sb;
	/* Deal with the last component. */
	if (lstat(p, &sb) == 0) {
		if (S_ISLNK(sb.st_mode)) {
			int n;
			if (++symlinks > MAXSYMLINKS) {
				errno = ELOOP;
				goto err1;
			}
			n = readlink(p, resolved, MAXPATHLEN-1);
			if (n < 0)
				goto err1;
			resolved[n] = '\0';
			goto loop;
		}
		if (S_ISDIR(sb.st_mode)) {
			if (chdir(p) < 0)
				goto err1;
			p = "";
		}
	}
    }
#endif

	/*
	 * Save the last component name and get the full pathname of
	 * the current directory.
	 */
	(void)strcpy(wbuf, p);
	if (getcwd(resolved, MAXPATHLEN) == 0)
		goto err1;

	/*
	 * Join the two strings together, ensuring that the right thing
	 * happens if the last component is empty, or the dirname is root.
	 */
	if (resolved[0] == '/' && resolved[1] == '\0')
		rootd = 1;
	else
		rootd = 0;

	if (*wbuf) {
		if (strlen(resolved) + strlen(wbuf) + rootd + 1 > MAXPATHLEN) {
			errno = ENAMETOOLONG;
			goto err1;
		}
		if (rootd == 0)
			(void)strcat(resolved, "/");
		(void)strcat(resolved, wbuf);
	}

	/* Go back to where we came from. */
#ifdef HAS_FCHDIR
	if (fchdir(fd) < 0) {
		serrno = errno;
		goto err2;
	}
#else
	if (chdir(wd) < 0) {
		serrno = errno;
		goto err2;
	}
#endif

	/* It's okay if the close fails, what's an fd more or less? */
#ifdef HAS_FCHDIR
	(void)close(fd);
#endif
	return (resolved);

err1:	serrno = errno;
#ifdef HAS_FCHDIR
	(void)fchdir(fd);
#else
	(void)chdir(wd);
#endif

err2:
#ifdef HAS_FCHDIR
	(void)close(fd);
#endif
	errno = serrno;
	return (NULL);
#endif
}

MODULE = Cwd		PACKAGE = Cwd

PROTOTYPES: ENABLE

void
fastcwd()
PROTOTYPE: DISABLE
PPCODE:
{
    dXSTARG;
    getcwd_sv(TARG);
    XSprePUSH; PUSHTARG;
#ifndef INCOMPLETE_TAINTS
    SvTAINTED_on(TARG);
#endif
}

void
abs_path(pathsv=Nullsv)
    SV *pathsv
PPCODE:
{
    dXSTARG;
    char *path;
    char buf[MAXPATHLEN];

    path = pathsv ? SvPV_nolen(pathsv) : ".";

    if (bsd_realpath(path, buf)) {
        sv_setpvn(TARG, buf, strlen(buf));
        SvPOK_only(TARG);
	SvTAINTED_on(TARG);
    }
    else
        sv_setsv(TARG, &PL_sv_undef);

    XSprePUSH; PUSHTARG;
#ifndef INCOMPLETE_TAINTS
    SvTAINTED_on(TARG);
#endif
}
