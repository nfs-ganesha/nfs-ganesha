/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 *   Copyright (C) International Business Machines  Corp., 2010
 *   Author(s): Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef LUSTRE_HANDLE_H
#define LUSTRE_HANDLE_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stddef.h> /* For having offsetof defined */
#include <limits.h>

#include "fsal_types.h"
#include "fsal_api.h"

#ifdef HAVE_INCLUDE_LUSTREAPI_H
#include <lustre/lustreapi.h>
#include <lustre/lustre_user.h>
#else
#ifdef HAVE_INCLUDE_LIBLUSTREAPI_H
#include <lustre/liblustreapi.h>
#include <lustre/lustre_user.h>
#include <linux/quota.h>
#endif
#endif

#ifndef AT_FDCWD
#error "Very old kernel and/or glibc"
#endif

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH           0x1000
#endif

#ifndef O_PATH
#define O_PATH 010000000
#endif

#ifndef O_NOACCESS
#define O_NOACCESS O_ACCMODE
#endif

#ifndef LPX64
#define LPX64 "%#llx"
#endif

#define DFID_NOBRACE    LPX64":0x%x:0x%x"

struct lustre_file_handle {
	lustre_fid fid;
	dev_t fsdev;
};	 /**< FS object handle */

#define lustre_alloc_handle(fh)                                       \
	do {                                                          \
		(fh) = alloca(sizeof(struct lustre_file_handle));     \
		memset((fh), 0, (sizeof(struct lustre_file_handle))); \
	} while (0)

static inline int lustre_handle_to_path(char *mntpath,
					struct lustre_file_handle *handle,
					char *path)
{
	if (!mntpath || !handle || !path)
		return -1;

	/* A Lustre fid path is looking like
	 * <where_lustre_is_mounted>/.lustre/fid/0x200000400:0x469a:0x0
	 * the "0x200000400:0x469a:0x0" represent the displayed fid */
	return snprintf(path, PATH_MAX, "%s/.lustre/fid/" DFID_NOBRACE,
			mntpath, PFID(&handle->fid));
}

static inline int lustre_path_to_handle(const char *path,
					struct fsal_fsid__ fsdev,
					struct lustre_file_handle *out_handle)
{
	lustre_fid fid;

	if (!path || !out_handle)
		return -1;

	/* Here, call liblustreapi's magic */
	if (llapi_path2fid(path, &fid) != 0)
		return -1;

	out_handle->fid = fid;
	out_handle->fsdev = makedev(fsdev.major, fsdev.minor);

	return 1;
}

static inline int
lustre_name_to_handle_at(struct fsal_filesystem *fs,
			 struct lustre_file_handle *at_handle,
			 const char *name,
			 struct lustre_file_handle *out_handle, int flags)
{
	char path[PATH_MAX + 2];

	if (!fs || !at_handle || !name || !out_handle)
		return -1;

	lustre_handle_to_path(fs->path, at_handle, path);

	if (flags != AT_EMPTY_PATH) {
		strncat(path, "/", PATH_MAX);
		strncat(path, name, PATH_MAX);
	}

	return lustre_path_to_handle(path, fs->fsid, out_handle);
}

static inline int lustre_open_by_handle(char *mntpath,
					struct lustre_file_handle *handle,
					int flags)
{
	char path[PATH_MAX];

	lustre_handle_to_path(mntpath, handle, path);

	return open(path, flags);
}

static inline size_t lustre_sizeof_handle(struct lustre_file_handle *hdl)
{
	return (size_t) sizeof(struct lustre_file_handle);
}


int lustre_extract_fsid(struct lustre_file_handle *fh,
			enum fsid_type *fsid_type,
			struct fsal_fsid__ *fsid);

#endif				/* LUSTRE_HANDLE_H */
