/**	$MirOS: src/usr.sbin/mtree/spec.c,v 1.2 2005/04/13 20:30:16 tg Exp $ */
/*	$NetBSD: spec.c,v 1.6 1995/03/07 21:12:12 cgd Exp $	*/
/*	$OpenBSD: spec.c,v 1.22 2004/08/01 18:32:20 deraadt Exp $	*/

/*-
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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

#ifndef lint
#if 0
static const char sccsid[] = "@(#)spec.c	8.1 (Berkeley) 6/6/93";
#else
static const char rcsid[] = "$OpenBSD: spec.c,v 1.22 2004/08/01 18:32:20 deraadt Exp $";
#endif
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <vis.h>
#include "mtree.h"
#include "extern.h"

int lineno;				/* Current spec line number. */

static void	 set(char *, NODE *);
static void	 unset(char *, NODE *);

NODE *
spec(void)
{
	NODE *centry, *last;
	char *p;
	NODE ginfo, *root;
	int c_cur, c_next;
	char buf[2048];
	size_t len;

	centry = last = root = NULL;
	bzero(&ginfo, sizeof(ginfo));
	c_cur = c_next = 0;
	for (lineno = 1; fgets(buf, sizeof(buf), stdin);
	    ++lineno, c_cur = c_next, c_next = 0) {
		/* Skip empty lines. */
		if (buf[0] == '\n')
			continue;

		/* Find end of line. */
		if ((p = strchr(buf, '\n')) == NULL)
			error("line %d too long", lineno);

		/* See if next line is continuation line. */
		if (p[-1] == '\\') {
			--p;
			c_next = 1;
		}

		/* Null-terminate the line. */
		*p = '\0';

		/* Skip leading whitespace. */
		for (p = buf; *p && isspace(*p); ++p);

		/* If nothing but whitespace or comment char, continue. */
		if (!*p || *p == '#')
			continue;

#ifdef DEBUG
		(void)fprintf(stderr, "line %d: {%s}\n", lineno, p);
#endif
		if (c_cur) {
			set(p, centry);
			continue;
		}

		/* Grab file name, "$", "set", or "unset". */
		if ((p = strtok(p, "\n\t ")) == NULL)
			error("missing field");

		if (p[0] == '/')
			switch(p[1]) {
			case 's':
				if (strcmp(p + 1, "set"))
					break;
				set(NULL, &ginfo);
				continue;
			case 'u':
				if (strcmp(p + 1, "unset"))
					break;
				unset(NULL, &ginfo);
				continue;
			}

		if (strchr(p, '/'))
			error("slash character in file name");

		if (!strcmp(p, "..")) {
			/* Don't go up, if haven't gone down. */
			if (!root)
				goto noparent;
			if (last->type != F_DIR || last->flags & F_DONE) {
				if (last == root)
					goto noparent;
				last = last->parent;
			}
			last->flags |= F_DONE;
			continue;

noparent:		error("no parent node");
		}

		len = strlen(p) + 1;	/* NUL in struct _node */
		if ((centry = calloc(1, sizeof(NODE) + len - 1)) == NULL)
			error("%s", strerror(errno));
		*centry = ginfo;
#define	MAGIC	"?*["
		if (strpbrk(p, MAGIC))
			centry->flags |= F_MAGIC;
		if (strunvis(centry->name, p) == -1) {
			fprintf(stderr,
			    "mtree: filename (%s) encoded incorrectly\n", p);
			strlcpy(centry->name, p, len);
		}
		set(NULL, centry);

		if (!root) {
			last = root = centry;
			root->parent = root;
		} else if (last->type == F_DIR && !(last->flags & F_DONE)) {
			centry->parent = last;
			last = last->child = centry;
		} else {
			centry->parent = last->parent;
			centry->prev = last;
			last = last->next = centry;
		}
	}
	return (root);
}

static void
set(char *t, NODE *ip)
{
	int type;
	char *kw, *val = NULL;
	struct group *gr;
	struct passwd *pw;
	void *m;
	int value;
	u_int32_t fset, fclr;
	char *ep;
	size_t len;

	for (; (kw = strtok(t, "= \t\n")); t = NULL) {
		ip->flags |= type = parsekey(kw, &value);
		if (value && (val = strtok(NULL, " \t\n")) == NULL)
			error("missing value");
		switch(type) {
		case F_CKSUM:
			ip->cksum = strtoul(val, &ep, 10);
			if (*ep)
				error("invalid checksum %s", val);
			break;
		case F_MD5:
			ip->md5digest = strdup(val);
			if (!ip->md5digest)
				error("%s", strerror(errno));
			break;
#ifndef __INTERIX
		case F_FLAGS:
			if (!strcmp(val, "none")) {
				ip->file_flags = 0;
				break;
			}
			if (strtofflags(&val, &fset, &fclr))
				error("%s", strerror(errno));
			ip->file_flags = fset;
			break;
#endif
		case F_GID:
			ip->st_gid = strtoul(val, &ep, 10);
			if (*ep)
				error("invalid gid %s", val);
			break;
		case F_GNAME:
			if ((gr = getgrnam(val)) == NULL)
			    error("unknown group %s", val);
			ip->st_gid = gr->gr_gid;
			break;
		case F_IGN:
			/* just set flag bit */
			break;
		case F_MODE:
			if ((m = setmode(val)) == NULL)
				error("invalid file mode %s", val);
			ip->st_mode = getmode(m, 0);
			free(m);
			break;
		case F_NLINK:
			ip->st_nlink = strtoul(val, &ep, 10);
			if (*ep)
				error("invalid link count %s", val);
			break;
		case F_RMD160:
			ip->rmd160digest = strdup(val);
			if (!ip->rmd160digest)
				error("%s", strerror(errno));
			break;
		case F_SHA1:
			ip->sha1digest = strdup(val);
			if (!ip->sha1digest)
				error("%s", strerror(errno));
			break;
		case F_SIZE:
			ip->st_size = strtouq(val, &ep, 10);
			if (*ep)
				error("invalid size %s", val);
			break;
		case F_SLINK:
			len = strlen(val) + 1;
			if ((ip->slink = malloc(len)) == NULL)
				error("%s", strerror(errno));
			if (strunvis(ip->slink, val) == -1) {
				fprintf(stderr,
				    "mtree: filename (%s) encoded incorrectly\n", val);
				strlcpy(ip->slink, val, len);
			}
			break;
		case F_TIME:
			ip->st_mtimespec.tv_sec = strtoul(val, &ep, 10);
			if (*ep != '.')
				error("invalid time %s", val);
			val = ep + 1;
			ip->st_mtimespec.tv_nsec = strtoul(val, &ep, 10);
			if (*ep)
				error("invalid time %s", val);
			break;
		case F_TYPE:
			switch(*val) {
			case 'b':
				if (!strcmp(val, "block"))
					ip->type = F_BLOCK;
				break;
			case 'c':
				if (!strcmp(val, "char"))
					ip->type = F_CHAR;
				break;
			case 'd':
				if (!strcmp(val, "dir"))
					ip->type = F_DIR;
				break;
			case 'f':
				if (!strcmp(val, "file"))
					ip->type = F_FILE;
				if (!strcmp(val, "fifo"))
					ip->type = F_FIFO;
				break;
			case 'l':
				if (!strcmp(val, "link"))
					ip->type = F_LINK;
				break;
			case 's':
				if (!strcmp(val, "socket"))
					ip->type = F_SOCK;
				break;
			default:
				error("unknown file type %s", val);
			}
			break;
		case F_UID:
			ip->st_uid = strtoul(val, &ep, 10);
			if (*ep)
				error("invalid uid %s", val);
			break;
		case F_UNAME:
			if ((pw = getpwnam(val)) == NULL)
			    error("unknown user %s", val);
			ip->st_uid = pw->pw_uid;
			break;
		}
	}
}

static void
unset(char *t, NODE *ip)
{
	char *p;

	while ((p = strtok(t, "\n\t ")))
		ip->flags &= ~parsekey(p, NULL);
}
