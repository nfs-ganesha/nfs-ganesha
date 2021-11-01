// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * @file  fsal_internal.c
 * @date  $Date: 2006/01/17 14:20:07 $
 * @brief Defines the data that are to be accessed as extern by the fsal
 *        modules
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

#define FSAL_INTERNAL_ERROR(__error, __msg)				      \
({									      \
	if ((__error) == EUNATCH)					      \
		LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");	      \
									      \
	LogFullDebug(COMPONENT_FSAL, "%s returned errno=%d", __msg, __error); \
									      \
	fsalstat(posix2fsal_error(__error), __error);			      \
})

/*********************************************************************
 *
 *  GPFS FSAL char device driver interfaces
 *
 ********************************************************************/

/**
 *  @brief Close by fd
 *
 *  @param fd Open file descriptor
 *
 *  @return status of operation
 */
fsal_status_t fsal_internal_close(int fd, void *owner, int cflags)
{
	struct close_file_arg carg;

	/* fd should not be less than 3 */
	assert(fd >= 3);

	carg.mountdirfd = fd;
	carg.close_fd = fd;
	carg.close_flags = cflags;
	carg.close_owner = owner;
	if (op_ctx && op_ctx->client)
		carg.cli_ip = op_ctx->client->hostaddr_str;

	if (unlikely(gpfs_ganesha(OPENHANDLE_CLOSE_FILE, &carg) < 0))
		return FSAL_INTERNAL_ERROR(errno, "OPENHANDLE_CLOSE_FILE");

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Open a file by handle from in an open directory
 *
 *  @param dirfd Open file descriptor of parent directory
 *  @param gpfs_fh Opaque filehandle
 *  @param fd File descriptor opened by the function
 *  @param oflags Flags to open the file with
 *
 *  @return status of operation
 */
fsal_status_t
fsal_internal_handle2fd(int dirfd, struct gpfs_file_handle *gpfs_fh,
			int *fd, int oflags)
{
	struct open_arg oarg = {0};
	int rc;

	oarg.mountdirfd = dirfd;
	oarg.handle = gpfs_fh;
	oarg.flags = oflags;
	if (op_ctx && op_ctx->client)
		oarg.cli_ip = op_ctx->client->hostaddr_str;

	rc = gpfs_ganesha(OPENHANDLE_OPEN_BY_HANDLE, &oarg);
	if (unlikely(rc < 0))
		return FSAL_INTERNAL_ERROR(errno, "OPENHANDLE_OPEN_BY_HANDLE");

	*fd = rc;

	/* Make sure the fd is not less than 3 */
	assert(*fd >= 3);

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
			    struct gpfs_file_handle *gpfs_fh,
			    int expfd)
{
	struct name_handle_arg harg;

	if (!gpfs_fh)
		return fsalstat(ERR_FSAL_FAULT, 0);

	harg.handle = gpfs_fh;
	harg.handle->handle_size = GPFS_MAX_FH_SIZE;
	harg.handle->handle_version = OPENHANDLE_VERSION;
	harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
	harg.name = fs_name;
	harg.dfd = dfd;
	harg.expfd = expfd;
	harg.flag = 0;

	LogFullDebug(COMPONENT_FSAL, "Lookup handle at %d for %s", dfd,
		     fs_name);

	if (unlikely(gpfs_ganesha(OPENHANDLE_NAME_TO_HANDLE, &harg) < 0))
		return FSAL_INTERNAL_ERROR(errno, "OPENHANDLE_NAME_TO_HANDLE");

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
	struct get_handle_arg harg;

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

	if (unlikely(gpfs_ganesha(OPENHANDLE_GET_HANDLE, &harg) < 0))
		return FSAL_INTERNAL_ERROR(errno, "OPENHANDLE_GET_HANDLE");

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
	struct name_handle_arg harg = {0};

	if (!gpfs_fh)
		return fsalstat(ERR_FSAL_FAULT, 0);

	harg.handle = gpfs_fh;
	harg.handle->handle_size = GPFS_MAX_FH_SIZE;
	harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
	harg.handle->handle_version = OPENHANDLE_VERSION;
	harg.dfd = fd;

	LogFullDebug(COMPONENT_FSAL, "Lookup handle by fd for %d", fd);

	if (unlikely(gpfs_ganesha(OPENHANDLE_NAME_TO_HANDLE, &harg) < 0))
		return FSAL_INTERNAL_ERROR(errno, "OPENHANDLE_NAME_TO_HANDLE");

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
	struct link_fh_arg linkarg;

	if (!link_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	linkarg.mountdirfd = dirfd;
	linkarg.len = strlen(link_name);
	linkarg.name = link_name;
	linkarg.dir_fh = gpfs_fh;
	linkarg.dst_fh = gpfs_fh_tgt;
	if (op_ctx && op_ctx->client)
		linkarg.cli_ip = op_ctx->client->hostaddr_str;

	if (unlikely(gpfs_ganesha(OPENHANDLE_LINK_BY_FH, &linkarg) < 0))
		return FSAL_INTERNAL_ERROR(errno, "OPENHANDLE_LINK_BY_FH");

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
	struct stat_name_arg statarg;

	if (!stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	statarg.mountdirfd = dirfd;
	statarg.len = strlen(stat_name);
	statarg.name = stat_name;
	statarg.handle = gpfs_fh;
	statarg.buf = buf;
	if (op_ctx && op_ctx->client)
		statarg.cli_ip = op_ctx->client->hostaddr_str;

	if (unlikely(gpfs_ganesha(OPENHANDLE_STAT_BY_NAME, &statarg) < 0))
		return FSAL_INTERNAL_ERROR(errno, "OPENHANDLE_STAT_BY_NAME");

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
	struct stat_name_arg statarg;
	int rc, errsv;

	if (!stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	statarg.mountdirfd = dirfd;
	statarg.len = strlen(stat_name);
	statarg.name = stat_name;
	statarg.handle = gpfs_fh;
	statarg.buf = buf;
	if (op_ctx && op_ctx->client)
		statarg.cli_ip = op_ctx->client->hostaddr_str;

	fsal_set_credentials(&op_ctx->creds);

	rc = gpfs_ganesha(OPENHANDLE_UNLINK_BY_NAME, &statarg);
	errsv = errno;

	fsal_restore_ganesha_credentials();

	if (unlikely(rc < 0))
		return FSAL_INTERNAL_ERROR(errsv, "OPENHANDLE_UNLINK_BY_NAME");

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
	struct create_name_arg crarg = {0};
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	if (!stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	crarg.mountdirfd = export_fd;
	crarg.mode = mode;
	crarg.dev = posix_flags;
	crarg.len = strlen(stat_name);
	crarg.name = stat_name;
	crarg.new_fh = fh;
	crarg.new_fh->handle_size = GPFS_MAX_FH_SIZE;
	crarg.new_fh->handle_key_size = OPENHANDLE_KEY_LEN;
	crarg.new_fh->handle_version = OPENHANDLE_VERSION;
	crarg.buf = buf;
	crarg.dir_fh = container_of(dir_hdl, struct gpfs_fsal_obj_handle,
				    obj_handle)->handle;
	if (op_ctx && op_ctx->client)
		crarg.cli_ip = op_ctx->client->hostaddr_str;

	if (unlikely(gpfs_ganesha(OPENHANDLE_CREATE_BY_NAME, &crarg) < 0))
		return FSAL_INTERNAL_ERROR(errno, "OPENHANDLE_CREATE_BY_NAME");

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t
fsal_internal_mknode(struct fsal_obj_handle *dir_hdl, const char *stat_name,
		     mode_t mode, dev_t dev, struct gpfs_file_handle *fh,
		     struct stat *buf)
{
	struct create_name_arg crarg = {0};
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	if (!stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	crarg.mountdirfd = export_fd;
	crarg.mode = mode;
	crarg.dev = dev;
	crarg.len = strlen(stat_name);
	crarg.name = stat_name;
	crarg.new_fh = fh;
	crarg.new_fh->handle_size = GPFS_MAX_FH_SIZE;
	crarg.new_fh->handle_key_size = OPENHANDLE_KEY_LEN;
	crarg.new_fh->handle_version = OPENHANDLE_VERSION;
	crarg.buf = buf;
	crarg.dir_fh = container_of(dir_hdl, struct gpfs_fsal_obj_handle,
				    obj_handle)->handle;
	if (op_ctx && op_ctx->client)
		crarg.cli_ip = op_ctx->client->hostaddr_str;

	if (unlikely(gpfs_ganesha(OPENHANDLE_MKNODE_BY_NAME, &crarg) < 0))
		return FSAL_INTERNAL_ERROR(errno, "OPENHANDLE_MKNODE_BY_NAME");

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
	struct rename_fh_arg renamearg;
	int rc, errsv;

	if (!old_name || !new_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	renamearg.mountdirfd = dirfd;
	renamearg.old_len = strlen(old_name);
	renamearg.old_name = old_name;
	renamearg.new_len = strlen(new_name);
	renamearg.new_name = new_name;
	renamearg.old_fh = gpfs_fh_old;
	renamearg.new_fh = gpfs_fh_new;
	if (op_ctx && op_ctx->client)
		renamearg.cli_ip = op_ctx->client->hostaddr_str;

	fsal_set_credentials(&op_ctx->creds);

	rc = gpfs_ganesha(OPENHANDLE_RENAME_BY_FH, &renamearg);
	errsv = errno;

	fsal_restore_ganesha_credentials();

	if (unlikely(rc < 0))
		return FSAL_INTERNAL_ERROR(errsv, "OPENHANDLE_RENAME_BY_FH");

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
			size_t maxlen)
{
	struct readlink_fh_arg readlinkarg;
	int rc;

	readlinkarg.mountdirfd = dirfd;
	readlinkarg.handle = gpfs_fh;
	readlinkarg.buffer = buf;
	readlinkarg.size = maxlen - 1; /* reserve 1 for terminating 0-byte */

	rc = gpfs_ganesha(OPENHANDLE_READLINK_BY_FH, &readlinkarg);

	if (unlikely(rc < 0))
		return FSAL_INTERNAL_ERROR(errno, "OPENHANDLE_READLINK_BY_FH");

	buf[rc] = '\0';

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

	/* Try VERSION4 first, followed by VERSION3,2 */
	rc = gpfs_ganesha(OPENHANDLE_GET_VERSION4, NULL);
	if (rc != -1)
		return 0;

	rc = gpfs_ganesha(OPENHANDLE_GET_VERSION3, NULL);
	if (rc != -1)
		return 0;

	rc = gpfs_ganesha(OPENHANDLE_GET_VERSION2, NULL);
	if (rc != -1)
		return 0;

	LogMajor(COMPONENT_FSAL, "OPENHANDLE_GET_VERSION failed: %d", errno);
	return errno;
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
			 gpfsfsal_xstat_t *buffxstat, gpfs_acl_t *acl_buf,
			 unsigned int acl_buflen, uint32_t *expire_time_attr,
			 bool expire, bool use_acl)
{
	struct xstat_arg xstatarg = {0};
	int errsv;
	int rc;

	if (!gpfs_fh || !buffxstat)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* Initialize acl header so that GPFS knows what we want. */
	if (use_acl) {
		acl_buf->acl_level = 0;
		acl_buf->acl_version = GPFS_ACL_VERSION_NFS4;
		acl_buf->acl_type = GPFS_ACL_TYPE_NFS4;
		acl_buf->acl_len = acl_buflen;
		acl_buf->acl_nace = 0;
		xstatarg.acl = acl_buf;
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
	if (op_ctx && op_ctx->client)
		xstatarg.cli_ip = op_ctx->client->hostaddr_str;

	rc = gpfs_ganesha(OPENHANDLE_GET_XSTAT, &xstatarg);
	errsv = errno;
	LogDebug(COMPONENT_FSAL,
		 "GET_XSTAT returned, fd %d rc %d fh_size %d",
		 dirfd, rc, gpfs_fh->handle_size);

	if (rc < 0) {
		switch (errsv) {
		case ENODATA:
			/* For the special file that do not have ACL, GPFS
			   returns ENODATA. In this case, return okay with
			   stat.
			*/
			buffxstat->attr_valid = XATTR_STAT;
			LogFullDebug(COMPONENT_FSAL,
				     "GET_XSTAT retrieved only stat, not acl");
			return fsalstat(ERR_FSAL_NO_ERROR, 0);

		case ENOSPC:
			/* If the supplied acl buffer is too small, we
			 * get this errno! acl_len will be updated to
			 * the required length.
			 *
			 * Return success and let the caller check the length
			 */
			if (use_acl && acl_buf->acl_len > acl_buflen) {
				LogFullDebug(COMPONENT_FSAL,
					"GET_XSTAT returned buffer too small, passed len: %u, required len: %u, ",
					acl_buflen, acl_buf->acl_len);
				errno = 0;
				return fsalstat(ERR_FSAL_NO_ERROR, 0);
			}

			LogWarn(COMPONENT_FSAL,
				"GET_XSTAT returned bogus ENOSPC, passed len: %u, required len: %u",
				acl_buflen, acl_buf->acl_len);
			return fsalstat(ERR_FSAL_SERVERFAULT, errsv);

		default:
			/* Handle other errors. */
			LogFullDebug(COMPONENT_FSAL,
				     "GET_XSTAT returned errno:%d -- %s",
				     errsv, strerror(errsv));
			if (errsv == EUNATCH)
				LogFatal(COMPONENT_FSAL,
					"GPFS Returned EUNATCH");
			return fsalstat(posix2fsal_error(errsv), errsv);
		}
	}

	if (use_acl) {
		if (acl_buf->acl_nace > GPFS_ACL_MAX_NACES) {
			LogEvent(COMPONENT_FSAL,
				"No. of ACE's:%d higher than supported by GPFS",
				acl_buf->acl_nace);
			/* Fail the request if ACL is invalid*/
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}
		buffxstat->attr_valid = XATTR_FSID | XATTR_STAT | XATTR_ACL;
	} else {
		buffxstat->attr_valid = XATTR_FSID | XATTR_STAT;
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Set NFS4 ACL as well as stat.
 *
 *  @param dirfd Open file descriptor of parent directory
 *  @param gpfs_fh file handle of file
 *  @param attr_valid Attributes valid
 *  @param attr_changed Attributes changed
 *  @param buffxstat Buffer
 *
 *  @return status of operation
 */
fsal_status_t
fsal_set_xstat_by_handle(int dirfd,
			 struct gpfs_file_handle *gpfs_fh, int attr_valid,
			 int attr_changed, gpfsfsal_xstat_t *buffxstat,
			 gpfs_acl_t *acl_buf)
{
	struct xstat_arg xstatarg = {0};
	int errsv;
	int rc;

	if (!gpfs_fh || !buffxstat)
		return fsalstat(ERR_FSAL_FAULT, 0);

	xstatarg.attr_valid = attr_valid;
	xstatarg.mountdirfd = dirfd;
	xstatarg.handle = gpfs_fh;
	xstatarg.acl = acl_buf;
	xstatarg.attr_changed = attr_changed;
	xstatarg.buf = &buffxstat->buffstat;
	if (op_ctx && op_ctx->client)
		xstatarg.cli_ip = op_ctx->client->hostaddr_str;

	/* We explicitly do NOT do setfsuid/setfsgid here because truncate,
	   even to enlarge a file, doesn't actually allocate blocks. GPFS
	   implements sparse files, so blocks of all 0 will not actually
	   be allocated.
	 */
	fsal_set_credentials(&op_ctx->creds);

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
 *  @param gpfs_fh file handle of file
 *  @param size The size to truncate the file to
 *
 *  @return status of operation
 */
fsal_status_t
fsal_trucate_by_handle(int dirfd,
		       struct gpfs_file_handle *gpfs_fh, u_int64_t size)
{
	gpfsfsal_xstat_t buffxstat = {0};

	if (!gpfs_fh || !op_ctx)
		return fsalstat(ERR_FSAL_FAULT, 0);

	buffxstat.buffstat.st_size = size;

	return fsal_set_xstat_by_handle(dirfd, gpfs_fh, XATTR_STAT,
					XATTR_SIZE, &buffxstat, NULL);
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
 *  @brief Indicates if an FSAL error should be posted as an INFO level debug
 *         message.
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
