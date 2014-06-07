/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
#include <sys/types.h>
#include <sys/syscall.h>
#include <xfs/xfs.h>
#include <xfs/handle.h>
#include "ganesha_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "../vfs_methods.h"
#include "handle_syscalls.h"

void display_xfs_handle(struct display_buffer *dspbuf,
			struct vfs_file_handle *fh)
{
	xfs_handle_t *hdl = (xfs_handle_t *) fh->handle_data;

	display_printf(dspbuf,
		       "Handle len %hhu:"
		       " fsid=0x%016"PRIx32".0x%016"PRIx32
		       " fid_len=%"PRIu16
		       " fid_pad=%"PRIu16
		       " fid_gen=%"PRIu32
		       " fid_ino=%"PRIu64,
		       fh->handle_len,
		       hdl->ha_fsid.val[0],
		       hdl->ha_fsid.val[1],
		       hdl->ha_fid.fid_len,
		       hdl->ha_fid.fid_pad,
		       hdl->ha_fid.fid_gen,
		       hdl->ha_fid.fid_ino);
}

#define LogXFSHandle(fh)						\
	do {								\
		if (isMidDebug(COMPONENT_FSAL)) {			\
			char buf[256];					\
			struct display_buffer dspbuf =			\
					{sizeof(buf), buf, buf};	\
									\
			display_xfs_handle(&dspbuf, fh);		\
									\
			LogMidDebug(COMPONENT_FSAL, "%s", buf);		\
		}							\
	} while (0)

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
	xfs_handle_t *hdl = (xfs_handle_t *) fh->handle_data;
	void *data;
	size_t sz;

	if (fh->handle_len < sizeof(*hdl)) {
		errno = E2BIG;
		return -1;
	}

	/* Get the information pertinent to this inode, and
	 * the file handle for the reference fd.
	 */
	if ((xfs_fsal_bulkstat_inode(fd, ino, &bstat) < 0) ||
	    (fd_to_handle(fd, &data, &sz) < 0))
		return -1;

	/* Copy the fsid from the reference fd */
	memcpy(&hdl->ha_fsid, data, sizeof(xfs_fsid_t));

	/* Fill in the rest of the handle with the information
	 * pertinent to this inode.
	 */
	hdl->ha_fid.fid_len = sizeof(xfs_handle_t) -
			      sizeof(xfs_fsid_t) -
			      sizeof(hdl->ha_fid.fid_len);
	hdl->ha_fid.fid_pad = 0;
	hdl->ha_fid.fid_gen = bstat.bs_gen;
	hdl->ha_fid.fid_ino = bstat.bs_ino;

	fh->handle_len = sizeof(*hdl);

	free_handle(data, sz);
	return 0;
}

int vfs_open_by_handle(struct vfs_filesystem *fs,
		       vfs_file_handle_t *fh, int openflags,
		       fsal_errors_t *fsal_error)
{
	int fd;

	LogXFSHandle(fh);

	if (openflags == (O_PATH | O_NOACCESS))
		openflags = O_DIRECTORY;

	fd = open_by_handle(fh->handle_data, fh->handle_len, openflags);
	if (fd < 0) {
		fd = -errno;
		if (fd == -ENOENT)
			*fsal_error = posix2fsal_error(ESTALE);
		else
			*fsal_error = posix2fsal_error(-fd);
	}
	return fd;
}

int vfs_fd_to_handle(int fd, struct fsal_filesystem *fs,
		     vfs_file_handle_t *fh)
{
	void *data;
	size_t sz;
	int rv = 0;

	if (fd_to_handle(fd, &data, &sz) < 0)
		return -1;

	if (sz >= fh->handle_len) {
		errno = E2BIG;
		rv = -1;
	} else {
		memcpy(fh->handle_data, data, sz);
		fh->handle_len = sz;

		LogXFSHandle(fh);
	}
	free_handle(data, sz);
	return rv;
}

int vfs_name_to_handle(int fd,
		       struct fsal_filesystem *fs,
		       const char *name,
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

		retval = vfs_fd_to_handle(tmpfd, fs, fh);
		e = errno;
		close(tmpfd);
		errno = e;
	} else {
		retval = xfs_fsal_inode2handle(fd, stat.st_ino, fh);
	}
	LogXFSHandle(fh);
	return retval;
}

int vfs_readlink(struct vfs_fsal_obj_handle *hdl,
		 fsal_errors_t *ferr)
{
	char ldata[MAXPATHLEN + 1];
	int retval;
	LogXFSHandle(hdl->handle);
	retval = readlink_by_handle(hdl->handle->handle_data,
				    hdl->handle->handle_len,
				    ldata, sizeof(ldata));
	if (retval < 0) {
		retval = -errno;
		*ferr = posix2fsal_error(retval);
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

int vfs_extract_fsid(vfs_file_handle_t *fh,
		     enum fsid_type *fsid_type,
		     struct fsal_fsid__ *fsid)
{
	xfs_handle_t *hdl = (xfs_handle_t *) fh->handle_data;

	LogXFSHandle(fh);

	if (hdl->ha_fid.fid_pad != 0) {
		char handle_data[sizeof(struct fsal_fsid__)];
		int rc;

		*fsid_type = (enum fsid_type) (hdl->ha_fid.fid_pad - 1);

		memcpy(handle_data, &hdl->ha_fsid, sizeof(hdl->ha_fsid));
		memcpy(handle_data + sizeof(hdl->ha_fsid),
		       &hdl->ha_fid.fid_ino,
		       sizeof(hdl->ha_fid.fid_ino));

		rc = decode_fsid(handle_data,
				 sizeof(handle_data),
				 fsid,
				 *fsid_type);
		if (rc < 0) {
			errno = EINVAL;
			return rc;
		}

		return 0;
	}

	*fsid_type = FSID_TWO_UINT32;
	fsid->major = hdl->ha_fsid.val[0];
	fsid->minor = hdl->ha_fsid.val[1];

	return 0;
}

int vfs_encode_dummy_handle(vfs_file_handle_t *fh,
			    struct fsal_filesystem *fs)
{
	xfs_handle_t *hdl = (xfs_handle_t *) fh->handle_data;
	char handle_data[sizeof(struct fsal_fsid__)];
	int rc;

	memset(handle_data, 0, sizeof(handle_data));

	/* Pack fsid into handle_data */
	rc = encode_fsid(handle_data,
			 sizeof(handle_data),
			 &fs->fsid,
			 fs->fsid_type);

	if (rc < 0) {
		errno = EINVAL;
		return rc;
	}

	assert(sizeof(handle_data) ==
	       (sizeof(hdl->ha_fsid) + sizeof(hdl->ha_fid.fid_ino)));

	memcpy(&hdl->ha_fsid, handle_data, sizeof(hdl->ha_fsid));
	memcpy(&hdl->ha_fid.fid_ino,
	       handle_data + sizeof(hdl->ha_fsid),
	       sizeof(hdl->ha_fid.fid_ino));
	hdl->ha_fid.fid_len = sizeof(xfs_handle_t) -
			      sizeof(xfs_fsid_t) -
			      sizeof(hdl->ha_fid.fid_len);
	hdl->ha_fid.fid_pad = fs->fsid_type + 1;
	hdl->ha_fid.fid_gen = 0;
	fh->handle_len = sizeof(*hdl);

	LogXFSHandle(fh);

	return 0;
}

bool vfs_is_dummy_handle(vfs_file_handle_t *fh)
{
	xfs_handle_t *hdl = (xfs_handle_t *) fh->handle_data;

	return hdl->ha_fid.fid_pad != 0;
}

bool vfs_valid_handle(struct gsh_buffdesc *desc)
{
	xfs_handle_t *hdl = (xfs_handle_t *) desc->addr;
	bool fsid_type_ok = false;

	if ((desc->addr == NULL) ||
	    (desc->len != sizeof(xfs_handle_t)))
		return false;

	if (isMidDebug(COMPONENT_FSAL)) {
		char buf[256];
		struct display_buffer dspbuf = {sizeof(buf), buf, buf};

		display_printf(&dspbuf,
			       "Handle len %d: "
			       " fsid=0x%016"PRIx32".0x%016"PRIx32
			       " fid_len=%"PRIu16
			       " fid_pad=%"PRIu16
			       " fid_gen=%"PRIu32
			       " fid_ino=%"PRIu64,
			       (int) desc->len,
			       hdl->ha_fsid.val[0],
			       hdl->ha_fsid.val[1],
			       hdl->ha_fid.fid_len,
			       hdl->ha_fid.fid_pad,
			       hdl->ha_fid.fid_gen,
			       hdl->ha_fid.fid_ino);

		LogMidDebug(COMPONENT_FSAL, "%s", buf);
	}

	if (hdl->ha_fid.fid_pad != 0) {
		switch ((enum fsid_type) (hdl->ha_fid.fid_pad - 1)) {
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

		if (hdl->ha_fid.fid_gen != 0)
			return false;
	}

	return hdl->ha_fid.fid_len == (sizeof(xfs_handle_t) -
				       sizeof(xfs_fsid_t) -
				       sizeof(hdl->ha_fid.fid_len));
}

int vfs_get_root_handle(struct vfs_filesystem *vfs_fs,
			struct vfs_fsal_export *exp)
{
	enum fsid_type fsid_type;
	struct fsal_fsid__ fsid;
	int fd;
	int retval;
	void *data;
	size_t sz;
	vfs_file_handle_t *fh;

	vfs_alloc_handle(fh);

	if (path_to_fshandle(vfs_fs->fs->path, &data, &sz) < 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "Export root %s could not be established for XFS error %s",
			 vfs_fs->fs->path, strerror(retval));
		return retval;
	}

	fd = open(vfs_fs->fs->path, O_RDONLY | O_DIRECTORY);

	if (fd < 0) {
		retval = errno;
		LogMajor(COMPONENT_FSAL,
			 "Could not open XFS mount point %s: rc = %s (%d)",
			 vfs_fs->fs->path, strerror(retval), retval);
		return retval;
	}

	retval = vfs_fd_to_handle(fd, vfs_fs->fs, fh);

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
			"Could not re-index XFS file system fsid for %s",
			vfs_fs->fs->path);
		retval = -retval;
	}

errout:

	close(fd);

	return retval;
}

void vfs_fini(struct vfs_fsal_export *myself)
{
	return;
}

void vfs_init_export_ops(struct vfs_fsal_export *myself,
			 const char *export_path)
{
	return;
}

int vfs_init_export_pnfs(struct vfs_fsal_export *myself)
{
	return 0;
}
