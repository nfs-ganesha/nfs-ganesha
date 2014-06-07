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

#include <sys/mount.h>
#include "fsal_convert.h"
#include "../../vfs_methods.h"
#include "include/os/freebsd/syscalls.h"

/*
 * Currently all FreeBSD versions define MAXFIDSZ to be 16 which is not
 * sufficient for PanFS file handle. Following scheme ensures that on FreeBSD
 * platform we always use big enough data structure for holding file handle by
 * using our own file handle data structure instead of struct fhandle from
 * mount.h
 *
 */

#define MAXFIDSIZE 36

struct v_fid {
	u_short fid_len;	/* length of data in bytes */
	u_short fid_reserved;	/* force longword alignment */
	char fid_data[MAXFIDSIZE];	/* data (variable length) */
};

struct v_fhandle {
	fsid_t fh_fsid;		/* Filesystem id of mount point */
	struct v_fid fh_fid;	/* Filesys specific id */
};

static inline size_t vfs_sizeof_handle(struct v_fhandle *fh)
{
	return sizeof(fsid_t) +
	       sizeof(fh->fh_fid.fid_len) +
	       sizeof(fh->fh_fid.fid_reserved) +
	       fh->fh_fid.fid_len;
}

/* Verify handle size is large enough.
 * sizeof(fsid_t) == 8
 * sizeof(fid_len) == 2
 * sizeof(fid_reserved) == 2
 * fid_len == MAXFIDSIZE
 */
#if VFS_HANDLE_LEN < (8 + 2 + 2 + MAXFIDSIZE)
#error "VFS_HANDLE_LEN is too small"
#endif

void display_vfs_handle(struct display_buffer *dspbuf,
			struct vfs_file_handle *fh)
{
	struct v_fhandle *hdl = (struct v_fhandle *) fh->handle_data;
	int b_left;

	b_left = display_printf(dspbuf,
				"Handle len %hhu:"
				" fsid=0x%016"PRIx32".0x%016"PRIx32
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
			char buf[256];					\
			struct display_buffer dspbuf =			\
					{sizeof(buf), buf, buf};	\
									\
			display_vfs_handle(&dspbuf, fh);		\
									\
			LogMidDebug(COMPONENT_FSAL, "%s", buf);		\
		}							\
	} while (0)

static inline int vfs_fd_to_handle(int fd, struct fsal_filesystem *fs,
				   vfs_file_handle_t *fh, int *mnt_id)
{
	int error;
	struct v_fhandle *handle = (struct v_fhandle *) fh->handle_data;

	error = getfhat(fd, NULL, handle, AT_SYMLINK_FOLLOW);

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

	error = getfhat(atfd, (char *)name, handle, AT_SYMLINK_NOFOLLOW);

	if (error == 0)
		fh->handle_len = vfs_sizeof_handle(handle);

	return error;
}

int vfs_open_by_handle(struct vfs_filesystem *fs,
		       vfs_file_handle_t *fh, int openflags,
		       fsal_errors_t *fsal_error)
{
	int fd;

	fd = fhopen((struct fhandle *)fh->handle_data, openflags);

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

	if (hdl->ha_fid.fid_reserved != 0) {
		int rc;

		*fsid_type = (enum fsid_type) (hdl->ha_fid.fid_reserved - 1);

		rc = decode_fsid(hdl->handle_data,
				 sizeof(hdl->handle_data),
				 fsid,
				 *fsid_type);
		if (rc < 0) {
			errno = EINVAL;
			return rc;
		}

		return 0;
	}

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

	fh->handle_len = vfs_sizeof_handle(handle);

	LogVFSHandle(fh);

	return 0;
}

bool vfs_is_dummy_handle(vfs_file_handle_t *fh)
{
	struct v_fhandle *hdl = (struct v_fhandle *) fh->handle_data;

	return hdl->ha_fid.fid_reserved != 0;
}

bool vfs_valid_handle(struct gsh_buffdesc *desc)
{
	struct v_fhandle *hdl = (struct v_fhandle *) desc->addr;

	if ((desc->addr == NULL) ||
	    (desc->len < (sizeof(fsid_t) +
			  sizeof(fh->fh_fid.fid_len)
			  sizeof(fh->fh_fid.fid_reserved))))
		return false;

	if (isMidDebug(COMPONENT_FSAL)) {
		char buf[256];
		struct display_buffer dspbuf = {sizeof(buf), buf, buf};
		int b_left;

		b_left = display_printf(&dspbuf,
					"Handle len %d: "
					" fsid=0x%016"PRIx32".0x%016"PRIx32
					" fid_len=%"PRIu16
					" fid_pad=%"PRIu16
					(int) desc->len,
					hdl->fh_fsid.val[0],
					hdl->fh_fsid.val[1],
					hdl->fh_fid.fid_len,
					hdl->fh_fid.fid_reserved);

		if (b_left > 0) {
			display_opaque_value(dspbuf,
					     hdl->fh_fid.fid_data,
					     hdl->fh_fid.fid_len);
		}

		LogMidDebug(COMPONENT_FSAL, "%s", buf);
	}

	if (hdl->ha_fid.fid_reserved != 0) {
		switch ((enum fsid_type) (hdl->ha_fid.fid_reserved - 1)) {
		case FSID_NO_TYPE:
		case FSID_ONE_UINT64:
		case FSID_MAJOR_64:
		case FSID_TWO_UINT64:
		case FSID_TWO_UINT32:
		case FSID_DEVICE:
			fsid_type_ok = true;
			break;
		}

		if (!fsid_type_ok) {
			LogDebug(COMPONENT_FSAL,
				 "FSID Type %02"PRIu16" invalid",
				 hdl->ha_fid.fid_pad - 1);
			return false;
		}
	}

	return (desc->len >= (sizeof(fsid_t) +
			      sizeof(hdl->sizeof(hdl->fh_fid.fid_len))) &&
	       (desc->len == vfs_sizeof_handle(hdl));
}

int vfs_re_index(struct vfs_filesystem *vfs_fs,
		 struct vfs_fsal_export *exp)
{
	enum fsid_type fsid_type;
	struct fsal_fsid__ fsid;
	int retval;
	vfs_file_handle_t *fh;

	vfs_alloc_handle(fh);

	retval = vfs_fd_to_handle(vfs_fs->root_fd, vfs_fs->fs, fh);

	if (retval != 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "Get root handle for %s failed with %s (%d)",
			 vfs_fs->fs->path, strerror(retval), retval);
		goto errout;
	}

	(void) vfs_extract_fsid(fh, &fsid_type, &fsid);

	retval = re_index_fs_fsid(vfs_fs->fs, fsid_type,
				  fsid.major, fsid.minor);

	if (retval < 0) {
		LogCrit(COMPONENT_FSAL,
			"Could not re-index VFS file system fsid for %s",
			vfs_fs->fs->path);
		retval = -retval;
	}

errout:

	return retval;
}

/** @} */
