// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Contributeur: Sachin Bhamare sbhamare@panasas.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @file    memstream.c
 * @brief   Set of function to provide open_memstream() API on FreeBSD
 *
 * Following set of function facilitate open_memstream API which
 * really is a wrapper around funopen() call on FreeBSD
 * platform. These are taken from the implementation at
 * http://people.freebsd.org/~jhb/mcelog/memstream.c and all the
 * relevant licences apply.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include "abstract_mem.h"

struct memstream {
	char **cp;
	size_t *lenp;
	size_t offset;
};

static void memstream_grow(struct memstream *ms, size_t newsize)
{
	char *buf;

	if (newsize > *ms->lenp) {
		buf = gsh_realloc(*ms->cp, newsize + 1);
#ifdef DEBUG
		fprintf(stderr, "MS: %p growing from %zd to %zd\n", ms,
			*ms->lenp, newsize);
#endif
		memset(buf + *ms->lenp + 1, 0, newsize - *ms->lenp);
		*ms->cp = buf;
		*ms->lenp = newsize;
	}
}

static int memstream_read(void *cookie, char *buf, int len)
{
	struct memstream *ms;
	int tocopy;

	ms = cookie;
	memstream_grow(ms, ms->offset + len);
	tocopy = *ms->lenp - ms->offset;
	if (len < tocopy)
		tocopy = len;
	memcpy(buf, *ms->cp + ms->offset, tocopy);
	ms->offset += tocopy;
#ifdef DEBUG
	fprintf(stderr, "MS: read(%p, %d) = %d\n", ms, len, tocopy);
#endif
	return tocopy;
}

static int memstream_write(void *cookie, const char *buf, int len)
{
	struct memstream *ms;
	int tocopy;

	ms = cookie;
	memstream_grow(ms, ms->offset + len);
	tocopy = *ms->lenp - ms->offset;
	if (len < tocopy)
		tocopy = len;
	memcpy(*ms->cp + ms->offset, buf, tocopy);
	ms->offset += tocopy;
#ifdef DEBUG
	fprintf(stderr, "MS: write(%p, %d) = %d\n", ms, len, tocopy);
#endif
	return tocopy;
}

static fpos_t memstream_seek(void *cookie, fpos_t pos, int whence)
{
	struct memstream *ms;
#ifdef DEBUG
	size_t old;
#endif

	ms = cookie;
#ifdef DEBUG
	old = ms->offset;
#endif
	switch (whence) {
	case SEEK_SET:
		ms->offset = pos;
		break;
	case SEEK_CUR:
		ms->offset += pos;
		break;
	case SEEK_END:
		ms->offset = *ms->lenp + pos;
		break;
	}
#ifdef DEBUG
	fprintf(stderr, "MS: seek(%p, %zd, %d) %zd -> %zd\n", ms, pos, whence,
		old, ms->offset);
#endif
	return ms->offset;
}

static int memstream_close(void *cookie)
{

	gsh_free(cookie);
	return 0;
}

FILE *open_memstream(char **cp, size_t *lenp)
{
	struct memstream *ms;
	int save_errno;
	FILE *fp;

	*cp = NULL;
	*lenp = 0;
	ms = gsh_malloc(sizeof(*ms));
	ms->cp = cp;
	ms->lenp = lenp;
	ms->offset = 0;
	fp = funopen(ms, memstream_read, memstream_write, memstream_seek,
		     memstream_close);
	if (fp == NULL) {
		save_errno = errno;
		gsh_free(ms);
		errno = save_errno;
	}
	return fp;
}
