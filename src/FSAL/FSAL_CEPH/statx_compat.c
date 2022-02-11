// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2016 Red Hat, Inc. and/or its affiliates.
 * Author: Jeff Layton <jlayton@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <cephfs/libcephfs.h>
#include "common_utils.h"
#include "fsal_types.h"
#include "statx_compat.h"

static void posix2ceph_statx(struct stat *st, struct ceph_statx *stx)
{
	memset(stx, 0, sizeof(*stx));
	stx->stx_mask = CEPH_STATX_BASIC_STATS | CEPH_STATX_VERSION;
	stx->stx_blksize = st->st_blksize;
	stx->stx_nlink = st->st_nlink;
	stx->stx_uid = st->st_uid;
	stx->stx_gid = st->st_gid;
	stx->stx_mode = st->st_mode;
	stx->stx_ino = st->st_ino;
	stx->stx_size = st->st_size;
	stx->stx_blocks = st->st_blocks;
	stx->stx_dev = st->st_dev;
	stx->stx_rdev = st->st_rdev;
	stx->stx_atime = st->st_atim;
	stx->stx_ctime = st->st_ctim;
	stx->stx_mtime = st->st_mtim;
	stx->stx_version = timespec_to_nsecs(&st->st_ctim);
}

int fsal_ceph_ll_walk(struct ceph_mount_info *cmount, const char *name,
			Inode **i, struct ceph_statx *stx, bool full,
			const struct user_cred *cred)
{
	int		rc;
	struct stat	st;

	rc = ceph_ll_walk(cmount, name, i, &st);
	if (rc == 0)
		posix2ceph_statx(&st, stx);
	return rc;
}

int fsal_ceph_ll_getattr(struct ceph_mount_info *cmount, struct Inode *in,
			struct ceph_statx *stx, unsigned int want,
			const struct user_cred *cred)
{
	int		rc;
	struct stat	st;

	rc = ceph_ll_getattr(cmount, in, &st,
				cred->caller_uid, cred->caller_gid);
	if (rc == 0)
		posix2ceph_statx(&st, stx);
	return rc;
}

int fsal_ceph_ll_lookup(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, Inode **out, struct ceph_statx *stx,
			bool full, const struct user_cred *cred)
{
	int		rc;
	struct stat	st;

	rc = ceph_ll_lookup(cmount, parent, name, &st, out,
				cred->caller_uid, cred->caller_gid);
	if (rc == 0)
		posix2ceph_statx(&st, stx);
	return rc;
}

int fsal_ceph_ll_mkdir(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, mode_t mode, Inode **out,
			struct ceph_statx *stx, bool full,
			const struct user_cred *cred)
{
	int		rc;
	struct stat	st;

	rc = ceph_ll_mkdir(cmount, parent, name, mode, &st, out,
				cred->caller_uid, cred->caller_gid);
	if (rc == 0)
		posix2ceph_statx(&st, stx);
	return rc;
}

#ifdef USE_FSAL_CEPH_MKNOD
int fsal_ceph_ll_mknod(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, mode_t mode, dev_t rdev,
			Inode **out, struct ceph_statx *stx, bool full,
			const struct user_cred *cred)
{
	int		rc;
	struct stat	st;

	rc = ceph_ll_mknod(cmount, parent, name, mode, rdev, &st,
				out, cred->caller_uid, cred->caller_gid);
	if (rc == 0)
		posix2ceph_statx(&st, stx);
	return rc;
}
#endif /* USE_FSAL_CEPH_MKNOD */

int fsal_ceph_ll_symlink(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, const char *link_path,
			Inode **out, struct ceph_statx *stx, bool full,
			const struct user_cred *cred)
{
	int		rc;
	struct stat	st;

	rc = ceph_ll_symlink(cmount, parent, name, link_path, &st,
				out, cred->caller_uid, cred->caller_gid);
	if (rc == 0)
		posix2ceph_statx(&st, stx);
	return rc;
}

int fsal_ceph_ll_create(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, mode_t mode, int oflags,
			Inode **outp, Fh **fhp, struct ceph_statx *stx,
			bool full, const struct user_cred *cred)
{
	int		rc;
	struct stat	st;

	rc = ceph_ll_create(cmount, parent, name, mode, oflags, &st, outp,
				fhp, cred->caller_uid, cred->caller_gid);
	if (rc == 0)
		posix2ceph_statx(&st, stx);
	return rc;
}

int fsal_ceph_ll_setattr(struct ceph_mount_info *cmount, Inode *i,
			  struct ceph_statx *stx, unsigned int mask,
			  const struct user_cred *cred)
{
	struct stat	st;

	memset(&st, 0, sizeof(st));
	if (mask & CEPH_SETATTR_MODE)
		st.st_mode = stx->stx_mode;
	if (mask & CEPH_SETATTR_UID)
		st.st_uid = stx->stx_uid;
	if (mask & CEPH_SETATTR_GID)
		st.st_gid = stx->stx_gid;
	if (mask & CEPH_SETATTR_ATIME)
		st.st_atim = stx->stx_atime;
	if (mask & CEPH_SETATTR_MTIME)
		st.st_mtim = stx->stx_mtime;
	if (mask & CEPH_SETATTR_CTIME)
		st.st_ctim = stx->stx_ctime;
	if (mask & CEPH_SETATTR_SIZE)
		st.st_size = stx->stx_size;
	return ceph_ll_setattr(cmount, i, &st, mask,
				cred->caller_uid, cred->caller_gid);
}

int fsal_ceph_readdirplus(struct ceph_mount_info *cmount,
			  struct ceph_dir_result *dirp, Inode *dir,
			  struct dirent *de, struct ceph_statx *stx,
			  unsigned int want, unsigned int flags, Inode **out,
			  struct user_cred *cred)
{
	int		stmask, rc;
	struct stat	st;

	rc = ceph_readdirplus_r(cmount, dirp, de, &st, &stmask);
	if (rc <= 0)
		return rc;

	if (flags & AT_NO_ATTR_SYNC) {
		posix2ceph_statx(&st, stx);
	} else {
		rc = fsal_ceph_ll_lookup(cmount, dir, de->d_name, out, stx,
					 true, cred);
		if (rc >= 0)
			rc = 1;
	}
	return rc;
}
