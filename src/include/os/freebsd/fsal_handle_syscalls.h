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
 * @file include/os/freebsd/fsal_handle_syscalls.h
 * @brief System calls for the FreeBSD handle calls
 */

#ifndef HANDLE_FREEBSD_H
#define HANDLE_FREEBSD_H

#include <fsal_convert.h>
#include "../os/freebsd/syscalls.h"

#ifndef O_PATH
#define O_PATH 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

#ifndef O_NOACCESS
#define O_NOACCESS 0
#endif

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif

#define PANFS_HANDLE_SIZE 40
#if VFS_HANDLE_LEN < (PANFS_HANDLE_SIZE + 8)
#error "VFS_HANDLE_LEN is too small"
#endif

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

/*
 * vfs_file_handle_t has a leading type and byte count that is
 * not aligned the same as the BSD struct fhandle, which begins
 * instead with a struct fsid, which is two 32-bit ints, then
 * a 2-byte length, 2-bytes of pad, and finally the array of handle bytes.
 * PanFS doesn't fill in the length, oh by the way.
 * So we copy the struct fhandle into the handle array instead of
 * overlaying the whole type like the Linux code does it.
 */
#define VFS_BSD_HANDLE_INIT(_fh, _handle)				\
	do {								\
		_fh->handle_bytes = vfs_sizeof_handle(_fh);		\
		_fh->handle_type = 0;					\
		memcpy(&_fh->handle[0], &_handle, _fh->handle_bytes);	\
	} while (0)

static inline size_t vfs_sizeof_handle(vfs_file_handle_t *fh)
{
	return offsetof(struct fhandle, fh_fid)+PANFS_HANDLE_SIZE;
}

#define vfs_alloc_handle(fh)						\
	do {								\
		(fh) = alloca(sizeof(vfs_file_handle_t));		\
		memset((fh), 0, sizeof(vfs_file_handle_t));		\
		(fh)->handle_bytes = offsetof(struct fhandle, fh_fid) + \
			PANFS_HANDLE_SIZE;				\
	} while (0)

static inline int vfs_fd_to_handle(int fd, vfs_file_handle_t *fh, int *mnt_id)
{
	int error;
	struct v_fhandle handle;
	error = getfhat(fd, NULL, (struct fhandle *)&handle,
			AT_SYMLINK_FOLLOW);
	if (error == 0)
		VFS_BSD_HANDLE_INIT(fh, handle);

	return error;
}

static inline int vfs_name_to_handle_at(int atfd, const char *name,
					vfs_file_handle_t *fh)
{
	int error;
	struct v_fhandle handle;
	error =
	    getfhat(atfd, (char *)name, (struct fhandle *)&handle,
		    AT_SYMLINK_NOFOLLOW);
	if (error == 0)
		VFS_BSD_HANDLE_INIT(fh, handle);

	return error;
}

static inline int vfs_open_by_handle(int mountfd, vfs_file_handle_t *fh,
				     int flags)
{
	return fhopen((struct fhandle *)fh->handle, flags);
}

static inline int vfs_stat_by_handle(int mountfd, vfs_file_handle_t *fh,
				     struct stat *buf, int flags)
{
	int fd, ret;
	fd = vfs_open_by_handle(mountfd, fh, flags);
	if (fd < 0)
		return fd;
	/* BSD doesn't (yet) have AT_EMPTY_PATH support, so just use fstat() */
	ret = fstat(fd, buf);
	close(fd);
	return ret;
}

static inline int vfs_chown_by_handle(int mountfd, vfs_file_handle_t *fh,
				      uid_t owner, gid_t group)
{
	int fd, ret;
	fd = vfs_open_by_handle(mountfd, fh, (O_PATH | O_NOACCESS));
	if (fd < 0)
		return fd;
	/* BSD doesn't (yet) have AT_EMPTY_PATH support, so just use
	   fchown() */
	ret = fchown(fd, owner, group);
	close(fd);
	return ret;
}

static inline int vfs_link_by_handle(vfs_file_handle_t *fh, int srcfd,
				     const char *sname, int destdirfd,
				     const char *dname, int flags,
				     fsal_errors_t *fsal_error)
{
	int retval;
	struct fhandle *handle = (struct fhandle *)fh->handle;
	retval = fhlink(handle, destdirfd, dname);
	if (retval < 0) {
		retval = -errno;
		*fsal_error = posix2fsal_error(errno);
	}
	return retval;
}

static inline int vfs_readlink_by_handle(vfs_file_handle_t *fh, int srcfd,
					 const char *sname, char *buf,
					 size_t bufsize)
{
	struct fhandle *handle = (struct fhandle *)fh->handle;
	return fhreadlink(handle, buf, bufsize);
}

#endif				/* HANDLE_FREEBSD_H */
/** @} */
