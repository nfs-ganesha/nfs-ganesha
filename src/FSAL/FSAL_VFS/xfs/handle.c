/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_handle_syscalls.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include <xfs/xfs.h>
#include <xfs/handle.h>
#include "nlm_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "../vfs_methods.h"

/* defined by libhandle but no prototype in xfs/handle.h */

int fd_to_handle(int fd, void **hanp, size_t *hlen);

/* The code that follows is intended to fake a XFS handle from
 * the bulkstat data.
 * It may not be portable
 * I keep it for wanting of a better solution */

#define XFS_FSHANDLE_SZ             8
struct xfs_fshandle {
	char fsh_space[XFS_FSHANDLE_SZ];
};

/* private file handle - for use by open_by_fshandle */
#define XFS_FILEHANDLE_SZ           24
#define XFS_FILEHANDLE_SZ_FOLLOWING 14
#define XFS_FILEHANDLE_SZ_PAD       2
struct xfs_filehandle {
	struct xfs_fshandle fh_fshdl;	/* handle of fs containing this inode */
	int16_t fh_sz_following;	/* bytes in handle after this member */
	char fh_pad[XFS_FILEHANDLE_SZ_PAD];	/* padding, must be zeroed */
	__uint32_t fh_gen;	/* generation count */
	xfs_ino_t fh_ino;	/* 64 bit ino */
};

static int xfs_fsal_bulkstat_inode(int fd, xfs_ino_t ino, xfs_bstat_t *bstat)
{
	xfs_fsop_bulkreq_t bulkreq;
	__u64 i = ino;

	bulkreq.lastip = &i;
	bulkreq.icount = 1;
	bulkreq.ubuffer = bstat;
	bulkreq.ocount = NULL;
	return ioctl(fd, XFS_IOC_FSBULKSTAT_SINGLE, &bulkreq);
}

static int xfs_fsal_inode2handle(int fd, ino_t ino, vfs_file_handle_t *fh)
{
	xfs_bstat_t bstat;
	struct xfs_filehandle hdl;
	void *data, *fhdata;
	size_t sz, fhsz;
	int rv;

	if (fh->handle_bytes < sizeof(hdl)) {
		errno = E2BIG;
		return -1;
	}
	if ((xfs_fsal_bulkstat_inode(fd, ino, &bstat) < 0)
	    || (fd_to_handle(fd, &data, &sz) < 0))
		return -1;

	rv = handle_to_fshandle(data, sz, &fhdata, &fhsz);
	if (rv  >= 0) {
		memset(&hdl, 0, sizeof(hdl));
		memcpy(&hdl.fh_fshdl, fhdata, fhsz);
		hdl.fh_sz_following = XFS_FILEHANDLE_SZ_FOLLOWING;
		hdl.fh_gen = bstat.bs_gen;
		hdl.fh_ino = bstat.bs_ino;

		memcpy(fh->handle, &hdl, sizeof(hdl));
		fh->handle_bytes = sizeof(hdl);
		free_handle(fhdata, fhsz);
	}

	free_handle(data, sz);
	return rv;
}

/* Yet another copy of this function */
static int p2fsal_error(int posix_errorcode)
{

	switch (posix_errorcode) {
	case 0:
		return ERR_FSAL_NO_ERROR;

	case EPERM:
		return ERR_FSAL_PERM;

	case ENOENT:
		return ERR_FSAL_NOENT;

		/* connection error */
#ifdef _AIX_5
	case ENOCONNECT:
#elif defined _LINUX
	case ECONNREFUSED:
	case ECONNABORTED:
	case ECONNRESET:
#endif

		/* IO error */
	case EIO:

		/* too many open files */
	case ENFILE:
	case EMFILE:

		/* broken pipe */
	case EPIPE:

		/* all shown as IO errors */
		return ERR_FSAL_IO;

		/* no such device */
	case ENODEV:
	case ENXIO:
		return ERR_FSAL_NXIO;

		/* invalid file descriptor : */
	case EBADF:
		/* we suppose it was not opened... */

      /**
       * @todo: The EBADF error also happens when file
       *        is opened for reading, and we try writting in it.
       *        In this case, we return ERR_FSAL_NOT_OPENED,
       *        but it doesn't seems to be a correct error translation.
       */

		return ERR_FSAL_NOT_OPENED;

	case ENOMEM:
	case ENOLCK:
		return ERR_FSAL_NOMEM;

	case EACCES:
		return ERR_FSAL_ACCESS;

	case EFAULT:
		return ERR_FSAL_FAULT;

	case EEXIST:
		return ERR_FSAL_EXIST;

	case EXDEV:
		return ERR_FSAL_XDEV;

	case ENOTDIR:
		return ERR_FSAL_NOTDIR;

	case EISDIR:
		return ERR_FSAL_ISDIR;

	case EINVAL:
		return ERR_FSAL_INVAL;

	case EFBIG:
		return ERR_FSAL_FBIG;

	case ENOSPC:
		return ERR_FSAL_NOSPC;

	case EMLINK:
		return ERR_FSAL_MLINK;

	case EDQUOT:
		return ERR_FSAL_DQUOT;

	case ENAMETOOLONG:
		return ERR_FSAL_NAMETOOLONG;

/**
 * @warning
 * AIX returns EEXIST where BSD uses ENOTEMPTY;
 * We want ENOTEMPTY to be interpreted anyway on AIX plateforms.
 * Thus, we explicitely write its value (87).
 */
#ifdef _AIX
	case 87:
#else
	case ENOTEMPTY:
	case -ENOTEMPTY:
#endif
		return ERR_FSAL_NOTEMPTY;

	case ESTALE:
		return ERR_FSAL_STALE;

		/* Error code that needs a retry */
	case EAGAIN:
	case EBUSY:

		return ERR_FSAL_DELAY;

	case ENOTSUP:
		return ERR_FSAL_NOTSUPP;

	case EOVERFLOW:
		return ERR_FSAL_OVERFLOW;

	case EDEADLK:
		return ERR_FSAL_DEADLOCK;

	case EINTR:
		return ERR_FSAL_INTERRUPT;

	case EROFS:
		return ERR_FSAL_ROFS;

	default:

		/* other unexpected errors */
		return ERR_FSAL_SERVERFAULT;

	}

}

static int vfs_xfs_open_by_handle(struct fsal_export *exp,
				  vfs_file_handle_t *fh, int openflags,
				  fsal_errors_t *fsal_error)
{
	int fd;

	if (openflags == (O_PATH | O_NOACCESS))
		openflags = O_DIRECTORY;

	fd = open_by_handle(fh->handle, fh->handle_bytes, openflags);
	if (fd < 0) {
		fd = -errno;
		if (fd == -ENOENT)
			*fsal_error = ERR_FSAL_STALE;
		else
			*fsal_error = p2fsal_error(-fd);
	}
	return fd;
}

static int vfs_xfs_fd_to_handle(int fd, vfs_file_handle_t *fh)
{
	void *data;
	size_t sz;
	int rv = 0;

	if (fd_to_handle(fd, &data, &sz) < 0)
		return -1;

	if (sz >= fh->handle_bytes) {
		errno = E2BIG;
		rv = -1;
	} else {
		memcpy(fh->handle, data, sz);
		fh->handle_bytes = sz;
	}
	free_handle(data, sz);
	return rv;
}

static int vfs_xfs_name_to_handle(int fd, const char *name,
				  vfs_file_handle_t *fh)
{
	int retval;
	struct stat stat;

	if (fstatat(fd, name, &stat, AT_SYMLINK_NOFOLLOW) < 0)
		return -1;

	if (S_ISDIR(stat.st_mode) || S_ISREG(stat.st_mode)) {
		int e;
		int tmpfd = openat(fd, name, O_RDONLY | O_NOFOLLOW, 0600);

		if (tmpfd < 0)
			return -1;

		retval = vfs_xfs_fd_to_handle(tmpfd, fh);
		e = errno;
		close(tmpfd);
		errno = e;
	} else {
		retval = xfs_fsal_inode2handle(fd, stat.st_ino, fh);
	}
	return retval;
}

static int vfs_xfs_readlink(struct vfs_fsal_obj_handle *hdl,
			    fsal_errors_t *ferr)
{
	char ldata[MAXPATHLEN + 1];
	int retval = readlink_by_handle(hdl->handle->handle,
					hdl->handle->handle_bytes,
					ldata, sizeof(ldata));
	if (retval < 0) {
		retval = -errno;
		*ferr = p2fsal_error(retval);
		goto out;
	}

	ldata[retval] = '\0';

	hdl->u.symlink.link_content = gsh_strdup(ldata);
	if (hdl->u.symlink.link_content == NULL) {
		*ferr = ERR_FSAL_NOMEM;
		retval = -ENOMEM;
	} else {
		hdl->u.symlink.link_size = retval + 1;
		retval = 0;
	}
 out:
	return retval;
}

struct vfs_exp_handle_ops xfs_ops = {
	.vex_open_by_handle = vfs_xfs_open_by_handle,
	.vex_name_to_handle = vfs_xfs_name_to_handle,
	.vex_fd_to_handle = vfs_xfs_fd_to_handle,
	.vex_readlink = vfs_xfs_readlink
};

struct vfs_exp_handle_ops *get_handle_ops(char *mntdir)
{
	void *data;
	size_t sz;

	/* This is a secret handshake which libhandle requires to
	 * make sure open_by_handle will work */
	if (path_to_fshandle(mntdir, &data, &sz) < 0)
		return NULL;
	free_handle(data, sz);

	return &xfs_ops;
}
