/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/queue.h>

#ifdef PKG_COMPAT
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#endif

#include <err.h>
#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include "pkgcli.h"

struct pkg_entry {
	struct pkg *pkg;
	STAILQ_ENTRY(pkg_entry) next;
};

STAILQ_HEAD(pkg_head, pkg_entry);

void
usage_create(void)
{
	fprintf(stderr, "usage: pkg create [-n] [-f format] [-o outdir] "
		"[-p plist] [-r rootdir] -m manifestdir\n");
	fprintf(stderr, "       pkg create [-gnx] [-f format] [-o outdir] "
		"[-r rootdir] pkg-name ...\n");
	fprintf(stderr, "       pkg create [-n] [-f format] [-o outdir] "
		"[-r rootdir] -a\n\n");
	fprintf(stderr, "For more information see 'pkg help create'.\n");
}

static int
pkg_create_matches(int argc, char **argv, match_t match, pkg_formats fmt,
    const char * const outdir, const char * const rootdir, bool overwrite)
{
	int i, ret = EPKG_OK, retcode = EPKG_OK;
	const char *name, *version;
#ifndef PKG_COMPAT
	struct pkg *pkg = NULL;
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	int query_flags = PKG_LOAD_DEPS | PKG_LOAD_FILES |
	    PKG_LOAD_CATEGORIES | PKG_LOAD_DIRS | PKG_LOAD_SCRIPTS |
	    PKG_LOAD_OPTIONS | PKG_LOAD_MTREE | PKG_LOAD_LICENSES |
	    PKG_LOAD_USERS | PKG_LOAD_GROUPS | PKG_LOAD_SHLIBS;
#endif
	struct pkg_head head = STAILQ_HEAD_INITIALIZER(head);
	struct pkg_entry *e = NULL;
	char pkgpath[MAXPATHLEN];
	const char *format;
	bool foundone;

#ifndef PKG_COMPAT
	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}
#endif

	switch (fmt) {
	case TXZ:
		format = "txz";
		break;
	case TBZ:
		format = "tbz";
		break;
	case TGZ:
		format = "tgz";
		break;
	case TAR:
		format = "tar";
		break;
	}

	for (i = 0; i < argc || match == MATCH_ALL; i++) {
#ifndef PKG_COMPAT
		if (match == MATCH_ALL) {
			printf("Loading package list...\n");
			if ((it = pkgdb_query(db, NULL, match)) == NULL)
				goto cleanup;
			match = !MATCH_ALL;
		} else
			if ((it = pkgdb_query(db, argv[i], match)) == NULL)
				goto cleanup;

		foundone = false;
		while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
			if ((e = malloc(sizeof(struct pkg_entry))) == NULL)
				err(1, "malloc(pkg_entry)");
			e->pkg = pkg;
			pkg = NULL;
			STAILQ_INSERT_TAIL(&head, e, next);
			foundone = true;
		}
		if (!foundone)
			warnx("No installed package matching \"%s\" found\n",
			    argv[i]);

		pkgdb_it_free(it);
		if (ret != EPKG_END)
			retcode++;
#else
		const char *dbdir = getenv("PKG_DBDIR");
		struct dirent *dp;
		DIR *d;

		if (dbdir == NULL)
			dbdir = "/var/db/pkg";

		foundone = false;
		if (match == MATCH_ALL) {
			if ((d = opendir(dbdir)) == NULL) {
				retcode++;
				goto cleanup;
			}
			while ((dp = readdir(d)) != NULL) {
				if (dp->d_type == DT_DIR) {
					if ((e = malloc(sizeof(struct pkg_entry))) == NULL)
						err(1, "malloc(pkg_entry)");
					e->pkg = NULL;
					pkg_new(&e->pkg, PKG_OLD_FILE);
					snprintf(pkgpath, MAXPATHLEN, "%s/%s", dbdir, dp->d_name);
					ret = pkg_old_load_from_path(e->pkg, pkgpath);
					STAILQ_INSERT_TAIL(&head, e, next);
					foundone = true;
				}
			}
			closedir(d);
			match = !MATCH_ALL;
		} else {
			struct stat st;
			snprintf(pkgpath, MAXPATHLEN, "%s/%s", dbdir, argv[i]);
			if (stat(pkgpath, &st) == 0 && S_ISDIR(st.st_mode)) {
				foundone = true;
				if ((e = malloc(sizeof(struct pkg_entry))) == NULL)
					err(1, "malloc(pkg_entry)");
				e->pkg = NULL;
				pkg_new(&e->pkg, PKG_OLD_FILE);
				ret = pkg_old_load_from_path(e->pkg, pkgpath);
				STAILQ_INSERT_TAIL(&head, e, next);
				foundone = true;
			}
		}
#endif
	}

	while (!STAILQ_EMPTY(&head)) {
		e = STAILQ_FIRST(&head);
		STAILQ_REMOVE_HEAD(&head, next);

		pkg_get(e->pkg, PKG_NAME, &name, PKG_VERSION, &version);
		if (!overwrite) {
			snprintf(pkgpath, MAXPATHLEN, "%s/%s-%s.%s", outdir,
			    name, version, format);
			if (access(pkgpath, F_OK) == 0) {
				printf("%s-%s already packaged, skipping...\n",
				    name, version);
				pkg_free(e->pkg);
				free(e);
				continue;
			}
		}
		printf("Creating package for %s-%s\n", name, version);
		if (pkg_create_installed(outdir, fmt, rootdir, e->pkg) !=
		    EPKG_OK)
			retcode++;
		pkg_free(e->pkg);
		free(e);
	}

cleanup:
#ifndef PKG_COMPAT
	pkgdb_close(db);
#endif

	return (retcode);
}

/*
 * options:
 * -x: regex
 * -g: globbing
 * -r: rootdir for the package
 * -m: path to dir where to find the metadata
 * -f <format>: format could be txz, tgz, tbz or tar
 * -o: output directory where to create packages by default ./ is used
 */

int
exec_create(int argc, char **argv)
{
	match_t match = MATCH_EXACT;
	const char *outdir = NULL;
	const char *format = NULL;
	const char *rootdir = NULL;
	const char *manifestdir = NULL;
	char *plist = NULL;
	bool overwrite = true;
	pkg_formats fmt;
	int ch;

	while ((ch = getopt(argc, argv, "agxf:r:m:o:np:")) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'f':
			format = optarg;
			break;
		case 'o':
			outdir = optarg;
			break;
		case 'r':
			rootdir = optarg;
			break;
		case 'm':
			manifestdir = optarg;
			break;
		case 'n':
			overwrite = false;
			break;
		case 'p':
			plist = optarg;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (match != MATCH_ALL && manifestdir == NULL && argc == 0) {
		usage_create();
		return (EX_USAGE);
	}

	if (outdir == NULL)
		outdir = "./";

	if (format == NULL) {
#ifdef PKG_COMPAT
		fmt = TBZ;
#else
		fmt = TXZ;
#endif
	} else {
		if (format[0] == '.')
			++format;
		if (strcmp(format, "txz") == 0)
			fmt = TXZ;
		else if (strcmp(format, "tbz") == 0)
			fmt = TBZ;
		else if (strcmp(format, "tgz") == 0)
			fmt = TGZ;
		else if (strcmp(format, "tar") == 0)
			fmt = TAR;
		else {
			warnx("unknown format %s, using txz", format);
#ifdef PKG_COMPAT
			fmt = TBZ;
#else
			fmt = TXZ;
#endif
		}
	}

	if (manifestdir == NULL)
		return (pkg_create_matches(argc, argv, match, fmt, outdir,
		    rootdir, overwrite) == EPKG_OK ? EX_OK : EX_SOFTWARE);
	else
		return (pkg_create_staged(outdir, fmt, rootdir, manifestdir,
		    plist) == EPKG_OK ? EX_OK : EX_SOFTWARE);
}

