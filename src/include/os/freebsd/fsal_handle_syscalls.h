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

static inline int vfs_stat_by_handle(int mountfd, vfs_file_handle_t *fh,
				     struct stat *buf, int flags)
{
	/* BSD doesn't (yet) have AT_EMPTY_PATH support, so just use fstat() */
	ret = fstat(mountfd, buf);
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
