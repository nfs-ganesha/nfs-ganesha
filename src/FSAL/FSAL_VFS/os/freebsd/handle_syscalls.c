// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 *   Copyright (C) Panasas, Inc. 2011
 *   Author(s): Brent Welch <welch@panasas.com>
		Sachin Bhamare <sbhamare@panasas.com>
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later
 *   version.
 *
 *   This library can be distributed with a BSD license as well, just
 *   ask.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free
 *   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *   02111-1307 USA
 */

/**
 * @file FSAL/FSAL_VFS/os/freebsd/handle_syscalls.c
 * @brief System calls for the FreeBSD handle calls
 */

#include <misc/queue.h> /* avoid conflicts with sys/queue.h */
#include <sys/mount.h>
#include "fsal_convert.h"
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "../../vfs_methods.h"
#include <syscalls.h>
#include "fsal_handle_syscalls.h"

static inline size_t vfs_sizeof_handle(struct v_fhandle *fh)
{
	return sizeof(fh->fh_flags) +
	       sizeof(fsid_t) +
	       sizeof(fh->fh_fid.fid_len) +
	       sizeof(fh->fh_fid.fid_reserved) +
	       fh->fh_fid.fid_len;
}

/* Verify handle size is large enough.
 * sizeof(fh_flags) == 1
 * sizeof(fsid_t) == 8
 * sizeof(fid_len) == 2
 * sizeof(fid_reserved) == 2
 * fid_len == MAXFIDSZ
 */
#if VFS_HANDLE_LEN < (1 + 8 + 2 + 2 + MAXFIDSZ)
#error "VFS_HANDLE_LEN is too small"
#endif

void display_vfs_handle(struct display_buffer *dspbuf,
			struct vfs_file_handle *fh)
{
	struct v_fhandle *hdl = (struct v_fhandle *) fh->handle_data;
	int b_left;

	b_left = display_printf(dspbuf,
				"Handle len %hhu: fsid=0x%016"
				PRIx32".0x%016"PRIx32
				" fid_len=%"PRIu16
				" fid_pad=%"PRIu16,
				fh->handle_len,
				hdl->fh_fsid.val[0],
				hdl->fh_fsid.val[1],
				hdl->fh_fid.fid_len,
				hdl->fh_fid.fid_reserved);

	if (b_left <= 0)
		return;

	display_opaque_value(dspbuf,
			     hdl->fh_fid.fid_data,
			     hdl->fh_fid.fid_len);
}

#define LogVFSHandle(fh)						\
	do {								\
		if (isMidDebug(COMPONENT_FSAL)) {			\
			char buf[256] = "\0";				\
			struct display_buffer dspbuf =			\
					{sizeof(buf), buf, buf};	\
									\
			display_vfs_handle(&dspbuf, fh);		\
									\
			LogMidDebug(COMPONENT_FSAL, "%s", buf);		\
		}							\
	} while (0)

int vfs_fd_to_handle(int fd, struct fsal_filesystem *fs,
		     vfs_file_handle_t *fh)
{
	int error;
	struct v_fhandle *handle = (struct v_fhandle *) fh->handle_data;

	error = getfhat(fd, NULL, v_to_fhandle(handle), AT_SYMLINK_FOLLOW);

	if (error == 0)
		fh->handle_len = vfs_sizeof_handle(handle);

	return error;
}

int vfs_name_to_handle(int atfd,
		       struct fsal_filesystem *fs,
		       const char *name,
		       vfs_file_handle_t *fh)
{
	int error;
	struct v_fhandle *handle = (struct v_fhandle *) fh->handle_data;

	error = getfhat(atfd, (char *)name, v_to_fhandle(handle),
			AT_SYMLINK_NOFOLLOW);

	if (error == 0)
		fh->handle_len = vfs_sizeof_handle(handle);

	return error;
}

int vfs_open_by_handle(struct fsal_filesystem *fs,
		       vfs_file_handle_t *fh, int openflags,
		       fsal_errors_t *fsal_error)
{
	int fd;

	fd = fhopen(v_to_fhandle(fh->handle_data), openflags);

	if (fd < 0) {
		fd = -errno;
		if (fd == -ENOENT)
			fd = -ESTALE;
		*fsal_error = posix2fsal_error(-fd);
		LogDebug(COMPONENT_FSAL, "Failed with %s", strerror(-fd));
	}
	return fd;
}

int vfs_extract_fsid(vfs_file_handle_t *fh,
		     enum fsid_type *fsid_type,
		     struct fsal_fsid__ *fsid)
{
	struct v_fhandle *hdl = (struct v_fhandle *) fh->handle_data;

	LogVFSHandle(fh);

	*fsid_type = FSID_TWO_UINT32;
	fsid->major = hdl->fh_fsid.val[0];
	fsid->minor = hdl->fh_fsid.val[1];

	return 0;
}

int vfs_encode_dummy_handle(vfs_file_handle_t *fh,
			    struct fsal_filesystem *fs)
{
	struct v_fhandle *hdl = (struct v_fhandle *) fh->handle_data;
	int rc;

	hdl->fh_fsid.val[0] = 0;
	hdl->fh_fsid.val[1] = 0;
	rc = encode_fsid(hdl->fh_fid.fid_data,
			 sizeof(hdl->fh_fid.fid_data),
			 &fs->fsid,
			 fs->fsid_type);

	if (rc < 0) {
		errno = EINVAL;
		return rc;
	}

	hdl->fh_fid.fid_reserved = fs->fsid_type + 1;
	hdl->fh_fid.fid_len = rc;
	hdl->fh_flags = HANDLE_DUMMY;

	fh->handle_len = vfs_sizeof_handle(hdl);

	LogVFSHandle(fh);

	return 0;
}

bool vfs_is_dummy_handle(vfs_file_handle_t *fh)
{
	struct v_fhandle *hdl = (struct v_fhandle *) fh->handle_data;

	return hdl->fh_flags == HANDLE_DUMMY;
}

bool vfs_valid_handle(struct gsh_buffdesc *desc)
{
	struct v_fhandle *hdl = (struct v_fhandle *) desc->addr;

	if ((desc->addr == NULL) ||
	    (desc->len < (sizeof(fsid_t) +
			  sizeof(hdl->fh_fid.fid_len) +
			  sizeof(hdl->fh_fid.fid_reserved))))
		return false;

	if (isMidDebug(COMPONENT_FSAL)) {
		char buf[256] = "\0";
		struct display_buffer dspbuf = {sizeof(buf), buf, buf};
		int b_left;

		b_left = display_printf(&dspbuf,
					"Handle len %d: fsid=0x%016"
					PRIx32".0x%016"PRIx32
					" fid_len=%"PRIu16
					" fid_pad=%"PRIu16,
					(int) desc->len,
					hdl->fh_fsid.val[0],
					hdl->fh_fsid.val[1],
					hdl->fh_fid.fid_len,
					hdl->fh_fid.fid_reserved);

		if (b_left > 0) {
			display_opaque_value(&dspbuf,
					     hdl->fh_fid.fid_data,
					     hdl->fh_fid.fid_len);
		}

		LogMidDebug(COMPONENT_FSAL, "%s", buf);
	}

	return (desc->len >= (sizeof(fsid_t) +
			  sizeof(hdl->fh_fid.fid_len) +
			  sizeof(hdl->fh_fid.fid_reserved))) &&
	       (desc->len == vfs_sizeof_handle(hdl));
}

int vfs_re_index(struct fsal_filesystem *fs,
		 struct vfs_fsal_export *exp)
{
	enum fsid_type fsid_type;
	struct fsal_fsid__ fsid;
	int retval;
	vfs_file_handle_t *fh;

	vfs_alloc_handle(fh);

	retval = vfs_fd_to_handle(root_fd(fs), fs, fh);

	if (retval != 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "Get root handle for %s failed with %s (%d)",
			 fs->path, strerror(retval), retval);
		goto errout;
	}

	/* Extract fsid from the root handle and re-index the filesystem
	 * using it. This is because the file handle already has an fsid in
	 * it.
	 */
	(void) vfs_extract_fsid(fh, &fsid_type, &fsid);

	retval = re_index_fs_fsid(fs, fsid_type, &fsid);

	if (retval < 0) {
		LogCrit(COMPONENT_FSAL,
			"Could not re-index VFS file system fsid for %s",
			fs->path);
		retval = -retval;
	}

errout:

	return retval;
}

/** @} */
