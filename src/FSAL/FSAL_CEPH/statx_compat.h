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

#ifndef _FSAL_CEPH_STATX_COMPAT_H
#define _FSAL_CEPH_STATX_COMPAT_H

#include "fsal_types.h"

/*
 * Depending on what we'll be doing with the resulting statx structure, we
 * either set the mask for the minimum that construct_handle requires, or a
 * full set of attributes.
 *
 * Note that even though construct_handle accesses the stx_mode field, we
 * don't need to request CEPH_STATX_MODE here, as the type bits are always
 * accessible.
 */
#define CEPH_STATX_HANDLE_MASK	(CEPH_STATX_INO)
#define CEPH_STATX_ATTR_MASK	(CEPH_STATX_BASIC_STATS	|	\
				 CEPH_STATX_BTIME	|	\
				 CEPH_STATX_VERSION)

#ifdef USE_FSAL_CEPH_STATX
static inline UserPerm *
user_cred2ceph(const struct user_cred *cred)
{
	return ceph_userperm_new(cred->caller_uid, cred->caller_gid,
				 cred->caller_glen, cred->caller_garray);
}

static inline int
fsal_ceph_ll_walk(struct ceph_mount_info *cmount, const char *name,
			Inode **i, struct ceph_statx *stx, bool full,
			const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_walk(cmount, name, i, stx,
		full ? CEPH_STATX_ATTR_MASK : CEPH_STATX_HANDLE_MASK, 0, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_getattr(struct ceph_mount_info *cmount, struct Inode *in,
			struct ceph_statx *stx, unsigned int want,
			const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_getattr(cmount, in, stx, want, 0, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_lookup(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, Inode **out, struct ceph_statx *stx,
			bool full, const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_lookup(cmount, parent, name, out, stx,
			full ? CEPH_STATX_ATTR_MASK : CEPH_STATX_HANDLE_MASK,
			0, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_mkdir(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, mode_t mode, Inode **out,
			struct ceph_statx *stx, bool full,
			const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_mkdir(cmount, parent, name, mode, out, stx,
			full ? CEPH_STATX_ATTR_MASK : CEPH_STATX_HANDLE_MASK,
			0, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_mknod(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, mode_t mode, dev_t rdev,
			Inode **out, struct ceph_statx *stx, bool full,
			const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_mknod(cmount, parent, name, mode, rdev, out, stx,
			full ? CEPH_STATX_ATTR_MASK : CEPH_STATX_HANDLE_MASK,
			0, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_symlink(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, const char *link_path,
			Inode **out, struct ceph_statx *stx, bool full,
			const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_symlink(cmount, parent, name, link_path, out, stx,
			full ? CEPH_STATX_ATTR_MASK : CEPH_STATX_HANDLE_MASK,
			0, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_readlink(struct ceph_mount_info *cmount, Inode *in, char *buf,
		      size_t bufsize, const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_readlink(cmount, in, buf, bufsize, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_create(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, mode_t mode, int oflags,
			Inode **outp, Fh **fhp, struct ceph_statx *stx,
			bool full, const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_create(cmount, parent, name, mode, oflags, outp,
			fhp, stx,
			full ? CEPH_STATX_ATTR_MASK : CEPH_STATX_HANDLE_MASK,
			0, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_setattr(struct ceph_mount_info *cmount, Inode *i,
			  struct ceph_statx *stx, unsigned int mask,
			  const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_setattr(cmount, i, stx, mask, perms);
#ifdef USE_FSAL_CEPH_LL_SYNC_INODE
	if (!ret)
		ret = ceph_ll_sync_inode(cmount, i, 0);
#endif /* USE_FSAL_CEPH_LL_SYNC_INODE */
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_open(struct ceph_mount_info *cmount, Inode *i,
		  int flags, Fh **fh, const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_open(cmount, i, flags, fh, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_opendir(struct ceph_mount_info *cmount, struct Inode *in,
		     struct ceph_dir_result **dirpp,
		     const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_opendir(cmount, in, dirpp, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_link(struct ceph_mount_info *cmount, Inode *i, Inode *newparent,
		  const char *name, const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_link(cmount, i, newparent, name, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_unlink(struct ceph_mount_info *cmount, struct Inode *in,
		    const char *name, const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_unlink(cmount, in, name, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_rename(struct ceph_mount_info *cmount, struct Inode *parent,
		    const char *name, struct Inode *newparent,
		    const char *newname, const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_rename(cmount, parent, name, newparent, newname, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_rmdir(struct ceph_mount_info *cmount, struct Inode *in,
		   const char *name, const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_rmdir(cmount, in, name, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_readdirplus(struct ceph_mount_info *cmount,
		      struct ceph_dir_result *dirp, Inode *dir,
		      struct dirent *de, struct ceph_statx *stx,
		      unsigned int want, unsigned int flags, Inode **out,
		      struct user_cred *cred)
{
	return ceph_readdirplus_r(cmount, dirp, de, stx, want, flags, out);
}

static inline int
fsal_ceph_ll_getxattr(struct ceph_mount_info *cmount, struct Inode *in,
			const char *name, char *val, size_t size,
			const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_getxattr(cmount, in, name, val, size, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_setxattr(struct ceph_mount_info *cmount, struct Inode *in,
			const char *name, char *val, size_t size, int flags,
			const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_setxattr(cmount, in, name, val, size, flags, perms);
	ceph_userperm_destroy(perms);
	return ret;
}

static inline int
fsal_ceph_ll_removexattr(struct ceph_mount_info *cmount, struct Inode *in,
			 const char *name, const struct user_cred *creds)
{
	int ret;
	UserPerm *perms = user_cred2ceph(creds);

	if (!perms)
		return -ENOMEM;

	ret = ceph_ll_removexattr(cmount, in, name, perms);
	ceph_userperm_destroy(perms);
	return ret;
}
#else /* USE_FSAL_CEPH_STATX */

#ifndef AT_NO_ATTR_SYNC
#define AT_NO_ATTR_SYNC		0x4000
#endif /* AT_NO_ATTR_SYNC */

struct ceph_statx {
	uint32_t	stx_mask;
	uint32_t	stx_blksize;
	uint32_t	stx_nlink;
	uint32_t	stx_uid;
	uint32_t	stx_gid;
	uint16_t	stx_mode;
	uint64_t	stx_ino;
	uint64_t	stx_size;
	uint64_t	stx_blocks;
	dev_t		stx_dev;
	dev_t		stx_rdev;
	struct timespec	stx_atime;
	struct timespec	stx_ctime;
	struct timespec	stx_mtime;
	struct timespec	stx_btime;
	uint64_t	stx_version;
};

#define CEPH_STATX_MODE		0x00000001U     /* Want/got stx_mode */
#define CEPH_STATX_NLINK	0x00000002U     /* Want/got stx_nlink */
#define CEPH_STATX_UID		0x00000004U     /* Want/got stx_uid */
#define CEPH_STATX_GID		0x00000008U     /* Want/got stx_gid */
#define CEPH_STATX_RDEV		0x00000010U     /* Want/got stx_rdev */
#define CEPH_STATX_ATIME	0x00000020U     /* Want/got stx_atime */
#define CEPH_STATX_MTIME	0x00000040U     /* Want/got stx_mtime */
#define CEPH_STATX_CTIME	0x00000080U     /* Want/got stx_ctime */
#define CEPH_STATX_INO		0x00000100U     /* Want/got stx_ino */
#define CEPH_STATX_SIZE		0x00000200U     /* Want/got stx_size */
#define CEPH_STATX_BLOCKS	0x00000400U     /* Want/got stx_blocks */
#define CEPH_STATX_BASIC_STATS	0x000007ffU     /* posix stat struct fields */
#define CEPH_STATX_BTIME	0x00000800U     /* Want/got stx_btime */
#define CEPH_STATX_VERSION	0x00001000U     /* Want/got stx_version */
#define CEPH_STATX_ALL_STATS	0x00001fffU     /* All supported stats */

int fsal_ceph_ll_walk(struct ceph_mount_info *cmount, const char *name,
			Inode **i, struct ceph_statx *stx, bool full,
			const struct user_cred *cred);
int fsal_ceph_ll_getattr(struct ceph_mount_info *cmount, struct Inode *in,
			struct ceph_statx *stx, unsigned int want,
			const struct user_cred *cred);
int fsal_ceph_ll_lookup(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, Inode **out, struct ceph_statx *stx,
			bool full, const struct user_cred *cred);
int fsal_ceph_ll_mkdir(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, mode_t mode, Inode **out,
			struct ceph_statx *stx, bool full,
			const struct user_cred *cred);
#ifdef USE_FSAL_CEPH_MKNOD
int fsal_ceph_ll_mknod(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, mode_t mode, dev_t rdev,
			Inode **out, struct ceph_statx *stx, bool full,
			const struct user_cred *cred);
#endif /* USE_FSAL_CEPH_MKNOD */
int fsal_ceph_ll_symlink(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, const char *link_path,
			Inode **out, struct ceph_statx *stx, bool full,
			const struct user_cred *cred);
int fsal_ceph_ll_create(struct ceph_mount_info *cmount, Inode *parent,
			const char *name, mode_t mode, int oflags,
			Inode **outp, Fh **fhp, struct ceph_statx *stx,
			bool full, const struct user_cred *cred);
int fsal_ceph_ll_setattr(struct ceph_mount_info *cmount, Inode *i,
			 struct ceph_statx *stx, unsigned int mask,
			 const struct user_cred *cred);
int fsal_ceph_readdirplus(struct ceph_mount_info *cmount,
			  struct ceph_dir_result *dirp, Inode *dir,
			  struct dirent *de, struct ceph_statx *stx,
			  unsigned int want, unsigned int flags, Inode **out,
			  struct user_cred *cred);

static inline int
fsal_ceph_ll_readlink(struct ceph_mount_info *cmount, Inode *in, char *buf,
		      size_t bufsize, const struct user_cred *creds)
{
	return ceph_ll_readlink(cmount, in, buf, bufsize, creds->caller_uid,
				creds->caller_gid);
}

static inline int
fsal_ceph_ll_open(struct ceph_mount_info *cmount, Inode *i,
		  int flags, Fh **fh, const struct user_cred *cred)
{
	return ceph_ll_open(cmount, i, flags, fh, cred->caller_uid,
				cred->caller_gid);
}

static inline int
fsal_ceph_ll_opendir(struct ceph_mount_info *cmount, struct Inode *in,
		     struct ceph_dir_result **dirpp,
		     const struct user_cred *cred)
{
	return ceph_ll_opendir(cmount, in, dirpp, cred->caller_uid,
				cred->caller_gid);
}

static inline int
fsal_ceph_ll_link(struct ceph_mount_info *cmount, Inode *i, Inode *newparent,
		  const char *name, const struct user_cred *cred)
{
	struct stat	st;

	return ceph_ll_link(cmount, i, newparent, name, &st, cred->caller_uid,
			    cred->caller_gid);
}

static inline int
fsal_ceph_ll_unlink(struct ceph_mount_info *cmount, struct Inode *in,
		    const char *name, const struct user_cred *cred)
{
	return ceph_ll_unlink(cmount, in, name, cred->caller_uid,
			      cred->caller_gid);
}

static inline int
fsal_ceph_ll_rename(struct ceph_mount_info *cmount, struct Inode *parent,
		    const char *name, struct Inode *newparent,
		    const char *newname, const struct user_cred *cred)
{
	return ceph_ll_rename(cmount, parent, name, newparent, newname,
				cred->caller_uid, cred->caller_gid);
}

static inline int
fsal_ceph_ll_rmdir(struct ceph_mount_info *cmount, struct Inode *in,
		   const char *name, const struct user_cred *cred)
{
	return ceph_ll_rmdir(cmount, in, name, cred->caller_uid,
			     cred->caller_gid);
}

static inline int
fsal_ceph_ll_getxattr(struct ceph_mount_info *cmount, struct Inode *in,
		      const char *name, char *val, size_t size,
		      const struct user_cred *cred)
{
	return ceph_ll_getxattr(cmount, in, name, val, size,
				cred->caller_uid, cred->caller_gid);
}

static inline int
fsal_ceph_ll_setxattr(struct ceph_mount_info *cmount, struct Inode *in,
		      const char *name, char *val, size_t size, int flags,
		      const struct user_cred *cred)
{
	return ceph_ll_setxattr(cmount, in, name, val, size, flags,
				cred->caller_uid, cred->caller_gid);
}

static inline int
fsal_ceph_ll_removexattr(struct ceph_mount_info *cmount, struct Inode *in,
			 const char *name, const struct user_cred *cred)
{
	return ceph_ll_removexattr(cmount, in, name, cred->caller_uid,
				   cred->caller_gid);
}
#endif /* USE_FSAL_CEPH_STATX */
#endif /* _FSAL_CEPH_STATX_COMPAT_H */
