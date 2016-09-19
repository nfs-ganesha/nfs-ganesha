/*
 * @file  fsal_internal.c
 * @date  $Date: 2006/01/17 14:20:07 $
 * @brief Defines the datas that are to be accessed as extern by the fsal modules
 *
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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

#include "config.h"

#include <sys/ioctl.h>
#include  "fsal.h"
#include "fsal_internal.h"
#include "gpfs_methods.h"
#include "fsal_convert.h"
#include <libgen.h>		/* used for 'dirname' */
#include "abstract_mem.h"

#include <pthread.h>
#include <string.h>
#include <sys/fsuid.h>

#include "include/gpfs.h"

/* credential lifetime (1h) */
uint32_t CredentialLifetime = 3600;

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
struct fsal_staticfsinfo_t global_fs_info;

/*********************************************************************
 *
 *  GPFS FSAL char device driver interaces
 *
 ********************************************************************/

/**
 *  @brief Open a file by handle within an export.
 *
 *  @param dirfd Descriptor
 *  @param gpfs_fh Opaque filehandle
 *  @param fd File descriptor openned by the function
 *  @param oflags Flags to open the file with
 *  @param reopen Bool specifying whether a reopen is wanted
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_handle2fd(int dirfd, struct gpfs_file_handle *gpfs_fh,
			int *pfd, int oflags, bool reopen)
{
	fsal_status_t status;

	if (!gpfs_fh || !pfd)
		return fsalstat(ERR_FSAL_FAULT, 0);

	status = fsal_internal_handle2fd_at(dirfd, gpfs_fh, pfd, oflags,
					    reopen);

	if (FSAL_IS_ERROR(status))
		return status;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Close by fd
 *
 *  @param fd Open file descriptor
 *
 *  @return status of operation
 */
fsal_status_t fsal_internal_close(int fd, void *owner, int cflags)
{
	int rc = 0;
	struct close_file_arg carg;
	int errsv = 0;

	carg.mountdirfd = fd;
	carg.close_fd = fd;
	carg.close_flags = cflags;
	carg.close_owner = owner;

	rc = gpfs_ganesha(OPENHANDLE_CLOSE_FILE, &carg);
	errsv = errno;

	LogFullDebug(COMPONENT_FSAL, "OPENHANDLE_CLOSE_FILE returned: rc %d",
		     rc);

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Open a file by handle from in an open directory
 *
 *  @param dirfd Open file descriptor of parent directory
 *  @param gpfs_fh Opaque filehandle
 *  @param fd File descriptor openned by the function
 *  @param oflags Flags to open the file with
 *  @param reopen Bool specifying whether a reopen is wanted
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_handle2fd_at(int dirfd, struct gpfs_file_handle *gpfs_fh,
			   int *fd, int oflags, bool reopen)
{
	int rc = 0;
	int errsv = 0;
	union {
		struct open_arg oarg;
		struct open_share_arg sarg;
	} u;

	if (!gpfs_fh || !fd)
		return fsalstat(ERR_FSAL_FAULT, 0);

	if (reopen) {
		u.sarg.mountdirfd = dirfd;
		u.sarg.handle = gpfs_fh;
		u.sarg.flags = oflags;
		u.sarg.openfd = *fd;
		/* share_access and share_deny are unused by REOPEN */
		u.sarg.share_access = 0;
		u.sarg.share_deny = 0;
		rc = gpfs_ganesha(OPENHANDLE_REOPEN_BY_FD, &u.sarg);
		errsv = errno;
		LogFullDebug(COMPONENT_FSAL,
			     "OPENHANDLE_REOPEN_BY_FD returned: rc %d", rc);
	} else {
		u.oarg.mountdirfd = dirfd;
		u.oarg.handle = gpfs_fh;
		u.oarg.flags = oflags;

		rc = gpfs_ganesha(OPENHANDLE_OPEN_BY_HANDLE, &u.oarg);
		errsv = errno;
		LogFullDebug(COMPONENT_FSAL,
			     "OPENHANDLE_OPEN_BY_HANDLE returned: rc %d", rc);
	}

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	/* gpfs_open returns fd number for OPENHANDLE_OPEN_BY_HANDLE,
	 * but only returns 0 for success for OPENHANDLE_REOPEN_BY_FD
	 * operation. We already have correct (*fd) in reopen case!
	 */
	if (!reopen)
		*fd = rc;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Create a handle from a directory pointer and filename
 *
 *  @param dfd Open directory handle
 *  @param fs_name Name of the file
 *  @param gpfs_fh The handle that is found and returned
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_get_handle_at(int dfd, const char *fs_name,
			    struct gpfs_file_handle *gpfs_fh)
{
	int rc;
	struct name_handle_arg harg;
	int errsv = 0;

	if (!gpfs_fh)
		return fsalstat(ERR_FSAL_FAULT, 0);

	harg.handle = gpfs_fh;
	harg.handle->handle_size = GPFS_MAX_FH_SIZE;
	harg.handle->handle_version = OPENHANDLE_VERSION;
	harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
	harg.name = fs_name;
	harg.dfd = dfd;
	harg.flag = 0;

	LogFullDebug(COMPONENT_FSAL, "Lookup handle at for %d %s", dfd,
		     fs_name);

	rc = gpfs_ganesha(OPENHANDLE_NAME_TO_HANDLE, &harg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Create a handle from a directory handle and filename
 *
 *  @param dirfd Open file descriptor of parent directory
 *  @param gpfs_fh The handle for the parent directory
 *  @param fs_name Name of the file
 *  @param gpfs_fh_out The handle that is found and returned
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_get_fh(int dirfd, struct gpfs_file_handle *gpfs_fh,
		     const char *fs_name, struct gpfs_file_handle *gpfs_fh_out)
{
	int rc;
	struct get_handle_arg harg;
	int errsv = 0;

	if (!gpfs_fh_out || !gpfs_fh || !fs_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	harg.mountdirfd = dirfd;
	harg.dir_fh = gpfs_fh;
	harg.out_fh = gpfs_fh_out;
	harg.out_fh->handle_size = GPFS_MAX_FH_SIZE;
	harg.out_fh->handle_version = OPENHANDLE_VERSION;
	harg.out_fh->handle_key_size = OPENHANDLE_KEY_LEN;
	harg.len = strlen(fs_name);
	harg.name = fs_name;

	LogFullDebug(COMPONENT_FSAL, "Lookup handle for %s", fs_name);

	rc = gpfs_ganesha(OPENHANDLE_GET_HANDLE, &harg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief convert an fd to a handle
 *
 *  @param fd Open file descriptor for target file
 *  @param gpfs_fh The handle that is found and returned
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_fd2handle(int fd, struct gpfs_file_handle *gpfs_fh)
{
	int rc;
	struct name_handle_arg harg;
	int errsv = 0;

	if (!gpfs_fh)
		return fsalstat(ERR_FSAL_FAULT, 0);

	harg.handle = gpfs_fh;
	harg.handle->handle_size = GPFS_MAX_FH_SIZE;
	harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
	harg.handle->handle_version = OPENHANDLE_VERSION;
	harg.name = NULL;
	harg.dfd = fd;
	harg.flag = 0;

	LogFullDebug(COMPONENT_FSAL, "Lookup handle by fd for %d", fd);

	rc = gpfs_ganesha(OPENHANDLE_NAME_TO_HANDLE, &harg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Create a link based on a file fh, dir fh, and new name
 *
 *  @param dir_fd Open file descriptor of parent directory
 *  @param gpfs_fh_tgt file handle of target file
 *  @param gpfs_fh file handle of source directory
 *  @param link_name name for the new file
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_link_fh(int dirfd, struct gpfs_file_handle *gpfs_fh_tgt,
		      struct gpfs_file_handle *gpfs_fh, const char *link_name)
{
	int rc;
	struct link_fh_arg linkarg;
	int errsv = 0;

	if (!link_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	linkarg.mountdirfd = dirfd;
	linkarg.len = strlen(link_name);
	linkarg.name = link_name;
	linkarg.dir_fh = gpfs_fh;
	linkarg.dst_fh = gpfs_fh_tgt;

	rc = gpfs_ganesha(OPENHANDLE_LINK_BY_FH, &linkarg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 *  @brief Stat a file by name
 *
 *  @param dirfd Open file descriptor of parent directory
 *  @param gpfs_fh file handle of directory
 *  @param stat_name name to stat
 *  @param buf buffer reference to buffer
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_stat_name(int dirfd, struct gpfs_file_handle *gpfs_fh,
			const char *stat_name, struct stat *buf)
{
	int rc;
	struct stat_name_arg statarg;
	int errsv = 0;

	if (!stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	statarg.mountdirfd = dirfd;
	statarg.len = strlen(stat_name);
	statarg.name = stat_name;
	statarg.handle = gpfs_fh;
	statarg.buf = buf;

	rc = gpfs_ganesha(OPENHANDLE_STAT_BY_NAME, &statarg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Unlink a file/directory by name
 *
 *  @param dirfd Open file descriptor of parent directory
 *  @param gpfs_fh file handle of directory
 *  @param stat_name name to unlink
 *  @param buf reference to buffer
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_unlink(int dirfd, struct gpfs_file_handle *gpfs_fh,
		     const char *stat_name, struct stat *buf)
{
	int rc;
	struct stat_name_arg statarg;
	int errsv = 0;

	if (!stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	statarg.mountdirfd = dirfd;
	statarg.len = strlen(stat_name);
	statarg.name = stat_name;
	statarg.handle = gpfs_fh;
	statarg.buf = buf;

	rc = gpfs_ganesha(OPENHANDLE_UNLINK_BY_NAME, &statarg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Create a file/directory by name
 *
 *  @param dir_hdl file handle of directory
 *  @param stat_name name to create
 *  @param mode file type for mknode
 *  @param dev file dev for mknode
 *  @param fh file handle of new file
 *  @param buf file attributes of new file
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_create(struct fsal_obj_handle *dir_hdl, const char *stat_name,
		     mode_t mode, int posix_flags, struct gpfs_file_handle *fh,
		     struct stat *buf)
{
	int rc;
	struct create_name_arg crarg;
	struct gpfs_filesystem *gpfs_fs;
	struct gpfs_fsal_obj_handle *gpfs_hdl;
	int errsv = 0;

	if (!stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	gpfs_hdl =
	   container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = dir_hdl->fs->private_data;

	crarg.mountdirfd = gpfs_fs->root_fd;
	crarg.mode = mode;
	crarg.dev = posix_flags;
	crarg.len = strlen(stat_name);
	crarg.name = stat_name;
	crarg.dir_fh = gpfs_hdl->handle;
	crarg.new_fh = fh;
	crarg.new_fh->handle_size = GPFS_MAX_FH_SIZE;
	crarg.new_fh->handle_key_size = OPENHANDLE_KEY_LEN;
	crarg.new_fh->handle_version = OPENHANDLE_VERSION;
	crarg.buf = buf;

	rc = gpfs_ganesha(OPENHANDLE_CREATE_BY_NAME, &crarg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t
fsal_internal_mknode(struct fsal_obj_handle *dir_hdl, const char *stat_name,
		     mode_t mode, dev_t dev, struct gpfs_file_handle *fh,
		     struct stat *buf)
{
	int rc;
	struct create_name_arg crarg;
	struct gpfs_filesystem *gpfs_fs;
	struct gpfs_fsal_obj_handle *gpfs_hdl;
	int errsv = 0;

	if (!stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	gpfs_hdl =
	    container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = dir_hdl->fs->private_data;

	crarg.mountdirfd = gpfs_fs->root_fd;
	crarg.mode = mode;
	crarg.dev = dev;
	crarg.len = strlen(stat_name);
	crarg.name = stat_name;
	crarg.dir_fh = gpfs_hdl->handle;
	crarg.new_fh = fh;
	crarg.new_fh->handle_size = GPFS_MAX_FH_SIZE;
	crarg.new_fh->handle_key_size = OPENHANDLE_KEY_LEN;
	crarg.new_fh->handle_version = OPENHANDLE_VERSION;
	crarg.buf = buf;

	rc = gpfs_ganesha(OPENHANDLE_MKNODE_BY_NAME, &crarg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Rename old file name to new name
 *
 *  @param dirfd Open file descriptor of parent directory
 *  @param gpfs_fh_old file handle of old file
 *  @param gpfs_fh_new file handle of new directory
 *  @param old_name name for the old file
 *  @param new_name name for the new file
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_rename_fh(int dirfd, struct gpfs_file_handle *gpfs_fh_old,
			struct gpfs_file_handle *gpfs_fh_new,
			const char *old_name, const char *new_name)
{
	int rc;
	struct rename_fh_arg renamearg;
	int errsv = 0;

	if (!old_name || !new_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	renamearg.mountdirfd = dirfd;
	renamearg.old_len = strlen(old_name);
	renamearg.old_name = old_name;
	renamearg.new_len = strlen(new_name);
	renamearg.new_name = new_name;
	renamearg.old_fh = gpfs_fh_old;
	renamearg.new_fh = gpfs_fh_new;

	rc = gpfs_ganesha(OPENHANDLE_RENAME_BY_FH, &renamearg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Reads the contents of the link
 *
 *  @param dirfd Open file descriptor of parent directory
 *  @param gpfs_fh file handle of file
 *  @param buf Buffer
 *  @param maxlen Max length of buffer
 *
 *  @return status of operation
 */
fsal_status_t
fsal_readlink_by_handle(int dirfd, struct gpfs_file_handle *gpfs_fh, char *buf,
			size_t *maxlen)
{
	int rc;
	struct readlink_fh_arg readlinkarg;
	int errsv = 0;

	readlinkarg.mountdirfd = dirfd;
	readlinkarg.handle = gpfs_fh;
	readlinkarg.buffer = buf;
	readlinkarg.size = *maxlen;

	rc = gpfs_ganesha(OPENHANDLE_READLINK_BY_FH, &readlinkarg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	if (rc < *maxlen) {
		buf[rc] = '\0';
		*maxlen = rc;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief fsal_internal_version;
 *
 *  @return GPFS version
 */
int fsal_internal_version(void)
{
	int rc;
	int errsv = 0;

	rc = gpfs_ganesha(OPENHANDLE_GET_VERSION, &rc);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		LogDebug(COMPONENT_FSAL, "GPFS get version failed with rc %d",
			 rc);
	} else
		LogDebug(COMPONENT_FSAL, "GPFS get version %d", rc);

	return rc;
}

/**
 *  @brief Get NFS4 ACL as well as stat.
 *
 *  @param dirfd Open file descriptor of parent directory
 *  @param gpfs_fh file handle of file
 *  @param buffxstat Buffer
 *  @param expire_time_attr Expire time attributes
 *  @param expire Bool expire
 *  @param use_acl Bool whether to ACL is to be used
 *  @return status of operation
 */
fsal_status_t
fsal_get_xstat_by_handle(int dirfd, struct gpfs_file_handle *gpfs_fh,
			 gpfsfsal_xstat_t *buffxstat,
			 uint32_t *expire_time_attr, bool expire, bool use_acl)
{
	int rc;
	struct xstat_arg xstatarg;
	int errsv = 0;

	if (!gpfs_fh || !buffxstat)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* Initialize acl header so that GPFS knows what we want. */
	if (use_acl) {
		gpfs_acl_t *pacl_gpfs;

		pacl_gpfs = (gpfs_acl_t *) buffxstat->buffacl;
		pacl_gpfs->acl_level = 0;
		pacl_gpfs->acl_version = GPFS_ACL_VERSION_NFS4;
		pacl_gpfs->acl_type = GPFS_ACL_TYPE_NFS4;
		pacl_gpfs->acl_len = GPFS_ACL_BUF_SIZE;
		xstatarg.acl = pacl_gpfs;
		xstatarg.attr_valid = XATTR_STAT | XATTR_ACL;
	} else {
		xstatarg.acl = NULL;
		xstatarg.attr_valid = XATTR_STAT;
	}
	if (expire)
		xstatarg.attr_valid |= XATTR_EXPIRE;

	xstatarg.mountdirfd = dirfd;
	xstatarg.handle = gpfs_fh;
	xstatarg.attr_changed = 0;
	xstatarg.buf = &buffxstat->buffstat;
	xstatarg.fsid = (struct fsal_fsid *)&buffxstat->fsal_fsid;
	xstatarg.attr_valid |= XATTR_FSID;
	xstatarg.expire_attr = expire_time_attr;

	rc = gpfs_ganesha(OPENHANDLE_GET_XSTAT, &xstatarg);
	errsv = errno;
	LogDebug(COMPONENT_FSAL,
		 "gpfs_ganesha: GET_XSTAT returned, fd %d rc %d fh_size %d",
		 dirfd, rc, gpfs_fh->handle_size);

	if (rc < 0) {
		if (errsv == ENODATA) {
			/* For the special file that do not have ACL, GPFS
			   returns ENODATA. In this case, return okay with
			   stat.
			*/
			buffxstat->attr_valid = XATTR_STAT;
			LogFullDebug(COMPONENT_FSAL,
				     "retrieved only stat, not acl");
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		} else {
			/* Handle other errors. */
			LogFullDebug(COMPONENT_FSAL,
				     "fsal_get_xstat_by_handle returned errno:%d -- %s",
				     errsv, strerror(errsv));
			if (errsv == EUNATCH)
				LogFatal(COMPONENT_FSAL,
					"GPFS Returned EUNATCH");
			return fsalstat(posix2fsal_error(errsv), errsv);
		}
	}

	if (use_acl)
		buffxstat->attr_valid = XATTR_FSID | XATTR_STAT | XATTR_ACL;
	else
		buffxstat->attr_valid = XATTR_FSID | XATTR_STAT;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Set NFS4 ACL as well as stat.
 *
 *  @param dirfd Open file descriptor of parent directory
 *  @param op_ctx Context
 *  @param gpfs_fh file handle of file
 *  @param attr_valid Attributes valid
 *  @param attr_changed Attributes changed
 *  @param buffxstat Buffer
 *
 *  @return status of operation
 */
fsal_status_t
fsal_set_xstat_by_handle(int dirfd, const struct req_op_context *op_ctx,
			 struct gpfs_file_handle *gpfs_fh, int attr_valid,
			 int attr_changed, gpfsfsal_xstat_t *buffxstat)
{
	int rc, errsv;
	struct xstat_arg xstatarg;

	if (!gpfs_fh || !buffxstat)
		return fsalstat(ERR_FSAL_FAULT, 0);

	xstatarg.attr_valid = attr_valid;
	xstatarg.mountdirfd = dirfd;
	xstatarg.handle = gpfs_fh;
	xstatarg.acl = (gpfs_acl_t *) buffxstat->buffacl;
	xstatarg.attr_changed = attr_changed;
	xstatarg.buf = &buffxstat->buffstat;

	/* We explicitly do NOT do setfsuid/setfsgid here because truncate,
	   even to enlarge a file, doesn't actually allocate blocks. GPFS
	   implements sparse files, so blocks of all 0 will not actually
	   be allocated.
	 */
	fsal_set_credentials(op_ctx->creds);

	rc = gpfs_ganesha(OPENHANDLE_SET_XSTAT, &xstatarg);
	errsv = errno;

	fsal_restore_ganesha_credentials();

	LogDebug(COMPONENT_FSAL, "gpfs_ganesha: SET_XSTAT returned, rc = %d",
		 rc);

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @param dirfd Open file descriptor of parent directory
 *  @param op_ctx Context
 *  @param gpfs_fh file handle of file
 *  @param size Size
 *
 *  @return status of operation
 */
fsal_status_t
fsal_trucate_by_handle(int dirfd, const struct req_op_context *op_ctx,
		       struct gpfs_file_handle *gpfs_fh, u_int64_t size)
{
	int attr_valid;
	int attr_changed;
	gpfsfsal_xstat_t buffxstat;

	if (!gpfs_fh || !op_ctx)
		return fsalstat(ERR_FSAL_FAULT, 0);

	attr_valid = XATTR_STAT;
	attr_changed = XATTR_SIZE;
	buffxstat.buffstat.st_size = size;

	return fsal_set_xstat_by_handle(dirfd, op_ctx, gpfs_fh, attr_valid,
					attr_changed, &buffxstat);
}

/**
 *  @brief Indicates if an FSAL error should be posted as an event
 *
 *  @param status(input): The fsal status whom event is to be tested.
 *  @return - true if the error event is to be posted.
 *          - false if the error event is NOT to be posted.
 */
bool fsal_error_is_event(fsal_status_t status)
{

	switch (status.major) {

	case ERR_FSAL_IO:
	case ERR_FSAL_STALE:
		return true;

	default:
		return false;
	}
}

/**
 *  @brief Indicates if an FSAL error should be posted as an INFO level debug msg.
 *
 *  @param status(input): The fsal status whom event is to be tested.
 *  @return - true if the error event is to be posted.
 *          - false if the error event is NOT to be posted.
 */
bool fsal_error_is_info(fsal_status_t status)
{
	switch (status.major) {
	case ERR_FSAL_NOTDIR:
	case ERR_FSAL_NOMEM:
	case ERR_FSAL_FAULT:
	case ERR_FSAL_EXIST:
	case ERR_FSAL_XDEV:
	case ERR_FSAL_ISDIR:
	case ERR_FSAL_INVAL:
	case ERR_FSAL_FBIG:
	case ERR_FSAL_NOSPC:
	case ERR_FSAL_MLINK:
	case ERR_FSAL_NAMETOOLONG:
	case ERR_FSAL_STALE:
	case ERR_FSAL_NOTSUPP:
	case ERR_FSAL_OVERFLOW:
	case ERR_FSAL_DEADLOCK:
	case ERR_FSAL_INTERRUPT:
	case ERR_FSAL_SERVERFAULT:
		return true;

	default:
		return false;
	}
}
