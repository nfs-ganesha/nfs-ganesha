// SPDX-License-Identifier: BSD-4-Clause
/*
 * Copyright (c) 1980, 1989, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2001
 *      David Rufino <daverufino@btinternet.com>
 * Copyright (c) 2006
 *      Stanislav Sedov <ssedov@mbsd.msk.ru>
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
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
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

/* most of this was ripped from the mount(3) source */

#include "config.h"
#include <os/freebsd/mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include "abstract_mem.h"
#include "log.h"

static int pos = -1;
static int mntsize = -1;
static struct statfs *_mntbuf;
static struct mntent _mntent;

struct {
	int m_flag;
	const char *m_option;
} mntoptions[] = {
	{
	MNT_ASYNC, "async"}, {
	MNT_NOATIME, "noatime"}, {
	MNT_NOEXEC, "noexec"}, {
	MNT_NOSUID, "nosuid"}, {
	MNT_NOSYMFOLLOW, "nosymfollow"}, {
	MNT_SYNCHRONOUS, "sync"}, {
	MNT_UNION, "union"}, {
	MNT_NOCLUSTERR, "noclusterr"}, {
	MNT_NOCLUSTERW, "noclusterw"}, {
	MNT_SUIDDIR, "suiddir"},
#ifdef MNT_SNAPSHOT
	{
	MNT_SNAPSHOT, "snapshot"},
#endif
#ifdef MNT_MULTILABEL
	{
	MNT_MULTILABEL, "multilabel"},
#endif
#ifdef MNT_ACLS
	{
	MNT_ACLS, "acls"},
#endif
#ifdef MNT_NODEV
	{
	MNT_NODEV, "nodev"},
#endif
};

#define N_OPTS (sizeof(mntoptions) / sizeof(*mntoptions))

char *hasmntopt(const struct mntent *mnt, const char *option)
{
	int found;
	char *opt, *optbuf;

	optbuf = gsh_strdup(mnt->mnt_opts);
	found = 0;
	for (opt = optbuf; (opt = strtok(opt, " ")) != NULL; opt = NULL) {
		if (!strcasecmp(opt, option)) {
			opt = opt - optbuf + mnt->mnt_opts;
			gsh_free(optbuf);
			return opt;
		}
	}
	gsh_free(optbuf);
	return NULL;
}

static void catopt(char *buf, int *pos, size_t size, const char *s1)
{
	size_t newlen;

	if (s1 == NULL || *s1 == '\0')
		return;

	newlen = strlen(s1);

	if (*pos != 0 && (*pos + 1 + newlen) < size) {
		buf[*pos] = ' ';
		*pos = *pos + 1;
	}

	if ((*pos + newlen) < size) {
		memcpy(buf + *pos, s1, newlen + 1);
		*pos = *pos + newlen;
	}
}

static void flags2opts(int flags, char *buf, size_t size)
{
	char *res = NULL;
	int i, pos = 0;

	catopt(res, &pos, size, (flags & MNT_RDONLY) ? "ro" : "rw");

	for (i = 0; i < N_OPTS; i++)
		if (flags & mntoptions[i].m_flag)
			catopt(res, &pos, size, mntoptions[i].m_option);
}

static struct mntent *statfs_to_mntent(struct statfs *mntbuf)
{
	static char opts_buf[40];

	_mntent.mnt_fsname = mntbuf->f_mntfromname;
	_mntent.mnt_dir = mntbuf->f_mntonname;
	_mntent.mnt_type = mntbuf->f_fstypename;
	flags2opts(mntbuf->f_flags, opts_buf, sizeof(opts_buf));
	_mntent.mnt_opts = opts_buf;
	_mntent.mnt_freq = _mntent.mnt_passno = 0;
	return &_mntent;
}

struct mntent *getmntent(FILE *fp)
{
	int i;

	if (pos == -1 || mntsize == -1)
		mntsize = getmntinfo(&_mntbuf, MNT_WAIT);

	for (i = 0; i < mntsize; i++)
		LogFullDebug(COMPONENT_FSAL,
			     "%s", _mntbuf[i].f_mntfromname);

	++pos;

	if (pos == mntsize) {
		pos = mntsize = -1;
		return NULL;
	}

	return statfs_to_mntent(&_mntbuf[pos]);
}
