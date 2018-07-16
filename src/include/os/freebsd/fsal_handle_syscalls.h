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
#include "syscalls.h"

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

/*
 * Currently all FreeBSD versions define MAXFIDSZ to be 16 which is not
 * sufficient for PanFS file handle. Following scheme ensures that on FreeBSD
 * platform we always use big enough data structure for holding file handle by
 * using our own file handle data structure instead of struct fhandle from
 * mount.h
 *
 */

#ifdef __PanFS__
#define MAXFIDSIZE 36
#else
#define MAXFIDSIZE 16
#endif

#define HANDLE_DUMMY 0x20

struct v_fid {
	u_short fid_len;		/* length of data in bytes */
	u_short fid_reserved;		/* force longword alignment */
	char fid_data[MAXFIDSIZE];	/* data (variable length) */
};

struct v_fhandle {
	uint8_t fh_flags;		/* Handle flags */
	fsid_t fh_fsid;			/* Filesystem id of mount point */
	struct v_fid fh_fid;		/* Filesys specific id */
};

#define v_to_fhandle(hdl) ((struct fhandle *) \
			  ((char *)hdl + offsetof(struct v_fhandle, fh_fsid)))

static inline int vfs_stat_by_handle(int mountfd, struct stat *buf)
{
	int ret;
	/* BSD doesn't (yet) have AT_EMPTY_PATH support, so just use fstat() */
	ret = fstat(mountfd, buf);
	return ret;
}

static inline int vfs_link_by_handle(vfs_file_handle_t *fh,
				     int srcfd,
				     int destdirfd,
				     const char *dname)
{
	struct fhandle *handle = v_to_fhandle(fh->handle_data);

	return fhlink(handle, destdirfd, dname);
}

static inline int vfs_readlink_by_handle(vfs_file_handle_t *fh, int srcfd,
					 const char *sname, char *buf,
					 size_t bufsize)
{
	struct fhandle *handle = v_to_fhandle(fh->handle_data);

	return fhreadlink(handle, buf, bufsize);
}

#endif				/* HANDLE_FREEBSD_H */
/** @} */
