/*
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

/**
 *
 * \file    fsal_internal.c
 * \date    $Date: 2006/01/17 14:20:07 $
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 *
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

#ifdef _USE_NFS4_ACL
#define ACL_DEBUG_BUF_SIZE 256
#endif				/* _USE_NFS4_ACL */

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
 * fsal_internal_handle2fd:
 * Open a file by handle within an export.
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param phandle (input):
 *        Opaque filehandle
 * \param pfd (output):
 *        File descriptor openned by the function
 * \param oflags (input):
 *        Flags to open the file with
 *
 * \return status of operation
 */

fsal_status_t fsal_internal_handle2fd(int dirfd,
				      struct gpfs_file_handle *phandle,
				      int *pfd, int oflags, bool reopen)
{
	fsal_status_t status;

	if (!phandle || !pfd)
		return fsalstat(ERR_FSAL_FAULT, 0);

	status = fsal_internal_handle2fd_at(dirfd, phandle, pfd, oflags,
					    reopen);

	if (FSAL_IS_ERROR(status))
		return status;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_close:
 * Close by fd
 *
 * \param fd (input):
 *        Open file descriptor
 *
 * \return status of operation
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
 * fsal_internal_handle2fd_at:
 * Open a file by handle from in an open directory
 *
 * \param dirfd (input):
 *        Open file descriptor of parent directory
 * \param phandle (input):
 *        Opaque filehandle
 * \param pfd (output):
 *        File descriptor openned by the function
 * \param oflags (input):
 *        Flags to open the file with
 *
 * \return status of operation
 */

fsal_status_t fsal_internal_handle2fd_at(int dirfd,
					 struct gpfs_file_handle *phandle,
					 int *pfd, int oflags, bool reopen)
{
	int rc = 0;
	int errsv = 0;
	union {
		struct open_arg oarg;
		struct open_share_arg sarg;
	} u;

	if (!phandle || !pfd)
		return fsalstat(ERR_FSAL_FAULT, 0);

	if (reopen) {
		u.sarg.mountdirfd = dirfd;
		u.sarg.handle = phandle;
		u.sarg.flags = oflags;
		u.sarg.openfd = *pfd;
		/* share_access and share_deny are unused by REOPEN */
		u.sarg.share_access = 0;
		u.sarg.share_deny = 0;
		rc = gpfs_ganesha(OPENHANDLE_REOPEN_BY_FD, &u.sarg);
		errsv = errno;
		LogFullDebug(COMPONENT_FSAL,
			     "OPENHANDLE_REOPEN_BY_FD returned: rc %d", rc);
	} else {
		u.oarg.mountdirfd = dirfd;
		u.oarg.handle = phandle;
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
	 * operation. We already have correct (*pfd) in reopen case!
	 */
	if (!reopen)
		*pfd = rc;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_get_handle:
 * Create a handle from a file path
 *
 * \param pcontext (input):
 *        A context pointer for the root of the current export
 * \param p_fsalpath (input):
 *        Full path to the file
 * \param p_handle (output):
 *        The handle that is found and returned
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_get_handle(const char *p_fsalpath,	/* IN */
				       struct gpfs_file_handle *p_handle)
{				/* OUT */
	int rc;
	struct name_handle_arg harg;
	int errsv = 0;

	if (!p_handle || !p_fsalpath)
		return fsalstat(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
	memset(p_gpfs_handle, 0, sizeof(*p_gpfs_handle));
#endif

	harg.handle = p_handle;
	harg.handle->handle_size = OPENHANDLE_HANDLE_LEN;
	harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
	harg.handle->handle_version = OPENHANDLE_VERSION;
	harg.name = p_fsalpath;
	harg.dfd = AT_FDCWD;
	harg.flag = 0;

#ifdef _VALGRIND_MEMCHECK
	memset(harg.handle->f_handle, 0, harg.handle->handle_size);
#endif

	LogFullDebug(COMPONENT_FSAL, "Lookup handle for %s", p_fsalpath);

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
 * fsal_internal_get_handle_at:
 * Create a handle from a directory pointer and filename
 *
 * \param dfd (input):
 *        Open directory handle
 * \param p_fsalname (input):
 *        Name of the file
 * \param p_handle (output):
 *        The handle that is found and returned
 *
 * \return status of operation
 */

fsal_status_t fsal_internal_get_handle_at(int dfd, const char *p_fsalname,
					  struct gpfs_file_handle *p_handle)
{				/* OUT */
	int rc;
	struct name_handle_arg harg;
	int errsv = 0;

	if (!p_handle)
		return fsalstat(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
	memset(p_gpfs_handle, 0, sizeof(*p_gpfs_handle));
#endif

	harg.handle = p_handle;
	harg.handle->handle_size = OPENHANDLE_HANDLE_LEN;
	harg.handle->handle_version = OPENHANDLE_VERSION;
	harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
	harg.name = p_fsalname;
	harg.dfd = dfd;
	harg.flag = 0;

#ifdef _VALGRIND_MEMCHECK
	memset(harg.handle->f_handle, 0, harg.handle->handle_size);
#endif

	LogFullDebug(COMPONENT_FSAL, "Lookup handle at for %d %s", dfd,
		     p_fsalname);

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
 * fsal_internal_get_handle:
 * Create a handle from a directory handle and filename
 *
 * \param pcontext (input):
 *        A context pointer for the root of the current export
 * \param p_dir_handle (input):
 *        The handle for the parent directory
 * \param p_fsalname (input):
 *        Name of the file
 * \param p_handle (output):
 *        The handle that is found and returned
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_get_fh(int dirfd,	/* IN  */
				   struct gpfs_file_handle *p_dir_fh, /* IN  */
				   const char *p_fsalname,	/* IN  */
				   struct gpfs_file_handle *p_out_fh)
{				/* OUT */
	int rc;
	struct get_handle_arg harg;
	int errsv = 0;

	if (!p_out_fh || !p_dir_fh || !p_fsalname)
		return fsalstat(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
	memset(p_gpfs_out_fh, 0, sizeof(*p_gpfs_out_fh));
#endif

	harg.mountdirfd = dirfd;
	harg.dir_fh = p_dir_fh;
	harg.out_fh = p_out_fh;
	harg.out_fh->handle_size = OPENHANDLE_HANDLE_LEN;
	harg.out_fh->handle_version = OPENHANDLE_VERSION;
	harg.out_fh->handle_key_size = OPENHANDLE_KEY_LEN;
	harg.len = strlen(p_fsalname);
	harg.name = p_fsalname;

#ifdef _VALGRIND_MEMCHECK
	memset(harg.out_fh, 0, harg.out_fh->handle_size);
#endif

	LogFullDebug(COMPONENT_FSAL, "Lookup handle for %s", p_fsalname);

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
 * fsal_internal_fd2handle:
 * convert an fd to a handle
 *
 * \param fd (input):
 *        Open file descriptor for target file
 * \param p_handle (output):
 *        The handle that is found and returned
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_fd2handle(int fd,
				      struct gpfs_file_handle *p_handle)
{
	int rc;
	struct name_handle_arg harg;
	int errsv = 0;

	if (!p_handle)
		return fsalstat(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
	memset(p_handle, 0, sizeof(*p_handle));
#endif

	harg.handle = p_handle;
	harg.handle->handle_size = OPENHANDLE_HANDLE_LEN;
	harg.handle->handle_key_size = OPENHANDLE_KEY_LEN;
	harg.handle->handle_version = OPENHANDLE_VERSION;
	harg.name = NULL;
	harg.dfd = fd;
	harg.flag = 0;

#ifdef _VALGRIND_MEMCHECK
	memset(harg.handle->f_handle, 0, harg.handle->handle_size);
#endif

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
 * fsal_internal_link_fh:
 * Create a link based on a file fh, dir fh, and new name
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param p_target_handle (input):
 *          file handle of target file
 * \param p_dir_handle (input):
 *          file handle of source directory
 * \param name (input):
 *          name for the new file
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_link_fh(int dirfd,
				    struct gpfs_file_handle *p_target_handle,
				    struct gpfs_file_handle *p_dir_handle,
				    const char *p_link_name)
{
	int rc;
	struct link_fh_arg linkarg;
	int errsv = 0;

	if (!p_link_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	linkarg.mountdirfd = dirfd;
	linkarg.len = strlen(p_link_name);
	linkarg.name = p_link_name;
	linkarg.dir_fh = p_dir_handle;
	linkarg.dst_fh = p_target_handle;

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
 * fsal_internal_stat_name:
 * Stat a file by name
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param p_dir_handle (input):
 *          file handle of directory
 * \param name (input):
 *          name to stat
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_stat_name(int dirfd,
				      struct gpfs_file_handle *p_dir_handle,
				      const char *p_stat_name,
				      struct stat *buf)
{
	int rc;
	struct stat_name_arg statarg;
	int errsv = 0;

#ifdef _VALGRIND_MEMCHECK
	memset(buf, 0, sizeof(*buf));
#endif

	if (!p_stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	statarg.mountdirfd = dirfd;
	statarg.len = strlen(p_stat_name);
	statarg.name = p_stat_name;
	statarg.handle = p_dir_handle;
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
 * fsal_internal_unlink:
 * Unlink a file/directory by name
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param p_dir_handle (input):
 *          file handle of directory
 * \param name (input):
 *          name to unlink
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_unlink(int dirfd,
				   struct gpfs_file_handle *p_dir_handle,
				   const char *p_stat_name, struct stat *buf)
{
	int rc;
	struct stat_name_arg statarg;
	int errsv = 0;

	if (!p_stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	statarg.mountdirfd = dirfd;
	statarg.len = strlen(p_stat_name);
	statarg.name = p_stat_name;
	statarg.handle = p_dir_handle;
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
 * fsal_internal_create:
 * Create a file/directory by name
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param p_dir_handle (input):
 *          file handle of directory
 * \param name (input):
 *          name to create
 * \param mode & dev (input):
 *          file type and dev for mknode
 * \param fh & stat (outut):
 *          file handle of new file and attributes
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_create(struct fsal_obj_handle *dir_hdl,
				   const char *p_stat_name, mode_t mode,
				   dev_t dev,
				   struct gpfs_file_handle *p_new_handle,
				   struct stat *buf)
{
	int rc;
	struct create_name_arg crarg;
	struct gpfs_filesystem *gpfs_fs;
	struct gpfs_fsal_obj_handle *gpfs_hdl;
	int errsv = 0;
#ifdef _VALGRIND_MEMCHECK
	gpfsfsal_handle_t *p_handle = (gpfsfsal_handle_t *) p_new_handle;
#endif

	if (!p_stat_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
	memset(p_handle, 0, sizeof(*p_handle));
#endif

	gpfs_hdl =
	    container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = dir_hdl->fs->private;

	crarg.mountdirfd = gpfs_fs->root_fd;
	crarg.mode = mode;
	crarg.dev = dev;
	crarg.len = strlen(p_stat_name);
	crarg.name = p_stat_name;
	crarg.dir_fh = gpfs_hdl->handle;
	crarg.new_fh = p_new_handle;
	crarg.new_fh->handle_size = OPENHANDLE_HANDLE_LEN;
	crarg.new_fh->handle_key_size = OPENHANDLE_KEY_LEN;
	crarg.new_fh->handle_version = OPENHANDLE_VERSION;
	crarg.buf = buf;

#ifdef _VALGRIND_MEMCHECK
	memset(crarg.new_fh->f_handle, 0, crarg.new_fh->handle_size);
#endif

	rc = gpfs_ganesha(OPENHANDLE_CREATE_BY_NAME, &crarg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_internal_rename_fh:
 * Rename old file name to new name
 *
 * \param p_context (input):
 *        Pointer to current context.  Used to get export root fd.
 * \param p_old_handle (input):
 *          file handle of old file
 * \param p_new_handle (input):
 *          file handle of new directory
 * \param name (input):
 *          name for the old file
 * \param name (input):
 *          name for the new file
 *
 * \return status of operation
 */
fsal_status_t fsal_internal_rename_fh(int dirfd,
				      struct gpfs_file_handle *p_old_handle,
				      struct gpfs_file_handle *p_new_handle,
				      const char *p_old_name,
				      const char *p_new_name)
{
	int rc;
	struct rename_fh_arg renamearg;
	int errsv = 0;

	if (!p_old_name || !p_new_name)
		return fsalstat(ERR_FSAL_FAULT, 0);

	renamearg.mountdirfd = dirfd;
	renamearg.old_len = strlen(p_old_name);
	renamearg.old_name = p_old_name;
	renamearg.new_len = strlen(p_new_name);
	renamearg.new_name = p_new_name;
	renamearg.old_fh = p_old_handle;
	renamearg.new_fh = p_new_handle;

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
 * fsal_readlink_by_handle:
 * Reads the contents of the link
 *
 *
 * \return status of operation
 */

fsal_status_t fsal_readlink_by_handle(int dirfd,
				      struct gpfs_file_handle *p_handle,
				      char *__buf, size_t *maxlen)
{
	int rc;
	struct readlink_fh_arg readlinkarg;
	int errsv = 0;

#ifdef _VALGRIND_MEMCHECK
	memset(__buf, 0, maxlen);
#endif

	readlinkarg.mountdirfd = dirfd;
	readlinkarg.handle = p_handle;
	readlinkarg.buffer = __buf;
	readlinkarg.size = *maxlen;

	rc = gpfs_ganesha(OPENHANDLE_READLINK_BY_FH, &readlinkarg);
	errsv = errno;

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	if (rc < *maxlen) {
		__buf[rc] = '\0';
		*maxlen = rc;
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  fsal_internal_version;
 *
 * \return GPFS version
 */

int fsal_internal_version()
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
	}
	else
		LogDebug(COMPONENT_FSAL, "GPFS get version %d", rc);

	return rc;
}

/* Get NFS4 ACL as well as stat. For now, get stat only until NFS4 ACL
 * support is enabled. */
fsal_status_t fsal_get_xstat_by_handle(int dirfd,
				       struct gpfs_file_handle *p_handle,
				       gpfsfsal_xstat_t *p_buffxstat,
				       uint32_t *expire_time_attr,
				       bool expire)
{
	int rc;
	struct xstat_arg xstatarg;
	int errsv = 0;
#ifdef _USE_NFS4_ACL
	gpfs_acl_t *pacl_gpfs;
#endif				/* _USE_NFS4_ACL */

	if (!p_handle || !p_buffxstat)
		return fsalstat(ERR_FSAL_FAULT, 0);

#ifdef _VALGRIND_MEMCHECK
	memset(p_buffxstat, 0, sizeof(*p_buffxstat));
#endif

#ifdef _USE_NFS4_ACL
	/* Initialize acl header so that GPFS knows what we want. */
	pacl_gpfs = (gpfs_acl_t *) p_buffxstat->buffacl;
	pacl_gpfs->acl_level = 0;
	pacl_gpfs->acl_version = GPFS_ACL_VERSION_NFS4;
	pacl_gpfs->acl_type = GPFS_ACL_TYPE_NFS4;
	pacl_gpfs->acl_len = GPFS_ACL_BUF_SIZE;
	xstatarg.acl = pacl_gpfs;
	xstatarg.attr_valid = XATTR_STAT | XATTR_ACL;
#else
	xstatarg.acl = NULL;
	xstatarg.attr_valid = XATTR_STAT;
#endif /* _USE_NFS4_ACL */
	if (expire)
		xstatarg.attr_valid |= XATTR_EXPIRE;

	xstatarg.mountdirfd = dirfd;
	xstatarg.handle = p_handle;
	xstatarg.attr_changed = 0;
	xstatarg.buf = &p_buffxstat->buffstat;
	xstatarg.fsid = (struct fsal_fsid *)&p_buffxstat->fsal_fsid;
	xstatarg.attr_valid |= XATTR_FSID;
	xstatarg.expire_attr = expire_time_attr;

	rc = gpfs_ganesha(OPENHANDLE_GET_XSTAT, &xstatarg);
	errsv = errno;
	LogDebug(COMPONENT_FSAL,
		 "gpfs_ganesha: GET_XSTAT returned, fd %d rc %d fh_size %d",
		 dirfd, rc, p_handle->handle_size);

	if (rc < 0) {
		if (errsv == ENODATA) {
			/* For the special file that do not have ACL, GPFS
			   returns ENODATA. In this case, return okay with
			   stat.
			*/
			p_buffxstat->attr_valid = XATTR_STAT;
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
#ifdef _USE_NFS4_ACL
	p_buffxstat->attr_valid = XATTR_FSID | XATTR_STAT | XATTR_ACL;
#else
	p_buffxstat->attr_valid = XATTR_FSID | XATTR_STAT;
#endif

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Set NFS4 ACL as well as stat. For now, set stat only until NFS4 ACL
 * support is enabled. */
fsal_status_t fsal_set_xstat_by_handle(int dirfd,
				       const struct req_op_context *p_context,
				       struct gpfs_file_handle *p_handle,
				       int attr_valid, int attr_changed,
				       gpfsfsal_xstat_t *p_buffxstat)
{
	int rc, errsv;
	struct xstat_arg xstatarg;

	if (!p_handle || !p_buffxstat)
		return fsalstat(ERR_FSAL_FAULT, 0);

	xstatarg.attr_valid = attr_valid;
	xstatarg.mountdirfd = dirfd;
	xstatarg.handle = p_handle;
	xstatarg.acl = (gpfs_acl_t *) p_buffxstat->buffacl;
	xstatarg.attr_changed = attr_changed;
	xstatarg.buf = &p_buffxstat->buffstat;

	/* We explicitly do NOT do setfsuid/setfsgid here because truncate,
	   even to enlarge a file, doesn't actually allocate blocks. GPFS
	   implements sparse files, so blocks of all 0 will not actually
	   be allocated.
	 */
	rc = gpfs_ganesha(OPENHANDLE_SET_XSTAT, &xstatarg);
	errsv = errno;

	LogDebug(COMPONENT_FSAL, "gpfs_ganesha: SET_XSTAT returned, rc = %d",
		 rc);

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* trucate by handle */
fsal_status_t fsal_trucate_by_handle(int dirfd,
				     const struct req_op_context *p_context,
				     struct gpfs_file_handle *p_handle,
				     u_int64_t size)
{
	int attr_valid;
	int attr_changed;
	gpfsfsal_xstat_t buffxstat;

	if (!p_handle || !p_context)
		return fsalstat(ERR_FSAL_FAULT, 0);

	attr_valid = XATTR_STAT;
	attr_changed = XATTR_SIZE;
	buffxstat.buffstat.st_size = size;

	return fsal_set_xstat_by_handle(dirfd, p_context, p_handle, attr_valid,
					attr_changed, &buffxstat);
}

/**
 *  fsal_error_is_event:
 *  Indicates if an FSAL error should be posted as an event
 *  \param status(input): The fsal status whom event is to be tested.
 *  \return - TRUE if the error event is to be posted.
 *          - FALSE if the error event is NOT to be posted.
 */
bool fsal_error_is_event(fsal_status_t status)
{

	switch (status.major) {

	case ERR_FSAL_IO:
	case ERR_FSAL_STALE:
		return TRUE;

	default:
		return FALSE;
	}
}

/**
 *  fsal_error_is_info:
 *  Indicates if an FSAL error should be posted as an INFO level debug msg.
 *  \param status(input): The fsal status whom event is to be tested.
 *  \return - TRUE if the error event is to be posted.
 *          - FALSE if the error event is NOT to be posted.
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
		return TRUE;

	default:
		return FALSE;
	}
}
