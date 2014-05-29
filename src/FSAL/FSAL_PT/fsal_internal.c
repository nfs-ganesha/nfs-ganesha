/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_internal.c
 * Description: FSAL internal operations implementation
 * Author:      FSI IPC dev team
 * ----------------------------------------------------------------------------
 */
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 *
 * -------------
 */

#define FSAL_INTERNAL_C
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/ioctl.h>
#include  "fsal.h"
#include "fsal_internal.h"
#include "semaphore.h"
#include "fsal_convert.h"
#include "pt_methods.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>

#include "pt_ganesha.h"

/* credential lifetime (1h) */
uint32_t CredentialLifetime = 3600;

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
struct fsal_staticfsinfo_t global_fs_info;

static fsal_status_t fsal_internal_testAccess_no_acl(
		const struct req_op_context *p_context,	/* IN */
		fsal_accessflags_t access_type,	/* IN */
		struct attrlist *p_object_attributes);	/* IN */

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
fsal_status_t fsal_internal_handle2fd(const struct req_op_context *p_context,
				      struct pt_fsal_obj_handle *myself,
				      int *pfd, int oflags)
{
	fsal_status_t status;

	if (!myself || !pfd)
		return fsalstat(ERR_FSAL_FAULT, 0);

	status = fsal_internal_handle2fd_at(p_context, myself, pfd, oflags);

	if (FSAL_IS_ERROR(status))
		return status;

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
fsal_status_t fsal_internal_handle2fd_at(const struct req_op_context *
					 p_context,
					 struct pt_fsal_obj_handle *myself,
					 int *pfd, int oflags)
{
	int open_rc = 0;
	int stat_rc;
	int err = 0;
	char fsi_name[PATH_MAX];

	FSI_TRACE(FSI_DEBUG, "FSI - handle2fd_at\n");

	if (!myself || !pfd)
		return fsalstat(ERR_FSAL_FAULT, 0);

        ptfsal_print_handle(myself->handle->data.handle.f_handle);

	FSI_TRACE(FSI_DEBUG, "Handle Type: %d",
		  myself->handle->data.handle.handle_type);

	if (myself->handle->data.handle.handle_type != DIRECTORY) {
		FSI_TRACE(FSI_DEBUG,
			  "FSI - handle2fdat - opening regular file\n");
		open_rc =
		    ptfsal_open_by_handle(p_context, myself, oflags, 0777);
		if (open_rc < 0)
			err = errno;
	} else {
		stat_rc =
		    fsi_get_name_from_handle(p_context,
					     p_context->fsal_export,
					     myself->handle, fsi_name, NULL);
		if (stat_rc < 0) {
			err = errno;
			FSI_TRACE(FSI_DEBUG, "Handle to name failed handle %s",
				  myself->handle->data.handle.f_handle);
			return fsalstat(posix2fsal_error(err), err);
		}
		FSI_TRACE(FSI_DEBUG, "NAME: %s", fsi_name);
		open_rc =
		    ptfsal_opendir(p_context, p_context->fsal_export,
				   fsi_name, NULL, 0);
		if (open_rc < 0)
			err = errno;
	}

	FSI_TRACE(FSI_DEBUG, "File Descriptor = %d\n", open_rc);
	if (err == ENOENT)
		err = ESTALE;
	if (err != 0)
		errno = err;

	if (open_rc < 0)
		return fsalstat(posix2fsal_error(err), err);

	*pfd = open_rc;

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
fsal_status_t fsal_internal_get_handle(const struct req_op_context *p_context,
				       struct fsal_export *export,
				       const char *p_fsalpath,	/* IN */
				       ptfsal_handle_t *p_handle)
{				/* OUT */
	int rc;
	fsi_stat_struct buffstat;
	uint64_t *handlePtr;

	FSI_TRACE(FSI_NOTICE, "FSI - get_handle for path %s\n", p_fsalpath);

	if (!p_handle || !p_fsalpath)
		return fsalstat(ERR_FSAL_FAULT, 0);

	memset(p_handle, 0, sizeof(ptfsal_handle_t));
	rc = ptfsal_stat_by_name(p_context, export, p_fsalpath, &buffstat);

	FSI_TRACE(FSI_DEBUG, "Stat call return %d", rc);
	if (rc)
		return fsalstat(ERR_FSAL_NOENT, errno);
	memset(p_handle, 0, sizeof(ptfsal_handle_t));
	memcpy(p_handle->data.handle.f_handle,
	       &buffstat.st_persistentHandle.handle,
	       FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
	p_handle->data.handle.handle_size = FSI_CCL_PERSISTENT_HANDLE_N_BYTES;
	p_handle->data.handle.handle_version = OPENHANDLE_VERSION;
	p_handle->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
	p_handle->data.handle.handle_type = posix2fsal_type(buffstat.st_mode);

	handlePtr = (uint64_t *) p_handle->data.handle.f_handle;
	FSI_TRACE(FSI_NOTICE,
		  "FSI - fsal_internal_get_handle[0x%lx %lx %lx %lx] type %x\n",
		  handlePtr[0], handlePtr[1], handlePtr[2], handlePtr[3],
		  p_handle->data.handle.handle_type);

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
fsal_status_t
fsal_internal_get_handle_at(const struct req_op_context *p_context,
					  struct fsal_export *export,
					  int dfd, const char *p_fsalname,
					  ptfsal_handle_t *p_handle)
{				/* OUT */
	fsi_stat_struct buffstat;
	int stat_rc;
	char fsal_path[PATH_MAX];

	FSI_TRACE(FSI_DEBUG, "FSI - get_handle_at for %s\n", p_fsalname);

	if (!p_handle)
		return fsalstat(ERR_FSAL_FAULT, 0);

	memset(p_handle, 0, sizeof(ptfsal_handle_t));

	LogFullDebug(COMPONENT_FSAL, "Lookup handle at for %s", p_fsalname);

	FSI_TRACE(FSI_DEBUG,
		  "FSI - gethandleat OPENHANDLE_NAME_TO_HANDLE [%s] dfd %d\n",
		  p_fsalname, dfd);

	memset(fsal_path, 0, PATH_MAX);
	memcpy(fsal_path, p_fsalname, PATH_MAX);
	stat_rc = ptfsal_stat_by_name(p_context, export, fsal_path, &buffstat);

	if (stat_rc == 0) {
		memcpy(&p_handle->data.handle.f_handle,
		       &buffstat.st_persistentHandle.handle,
		       sizeof(p_handle->data.handle.f_handle));
		p_handle->data.handle.handle_size =
		    FSI_CCL_PERSISTENT_HANDLE_N_BYTES;
		p_handle->data.handle.handle_type =
		    posix2fsal_type(buffstat.st_mode);
		p_handle->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
		p_handle->data.handle.handle_version = OPENHANDLE_VERSION;
		FSI_TRACE(FSI_DEBUG, "Handle=%s",
			  p_handle->data.handle.f_handle);
	} else {
		return fsalstat(ERR_FSAL_NOENT, errno);
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
fsal_status_t fsal_internal_fd2handle(int fd, ptfsal_handle_t *p_handle)
{

	FSI_TRACE(FSI_DEBUG, "FSI - fd2handle\n");

	if (!p_handle)
		return fsalstat(ERR_FSAL_FAULT, 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_readlink_by_handle:
 * Reads the contents of the link
 *
 *
 * \return status of operation
 */
fsal_status_t fsal_readlink_by_handle(const struct req_op_context *p_context,
				      struct fsal_export *export,
				      ptfsal_handle_t *p_handle, char *__buf,
				      size_t maxlen)
{
	int rc;

	FSI_TRACE(FSI_DEBUG, "Begin - readlink_by_handle\n");

	memset(__buf, 0, maxlen);
	rc = ptfsal_readlink(p_handle, export, p_context, __buf);

	if (rc < 0)
		return fsalstat(rc, 0);

	FSI_TRACE(FSI_DEBUG, "End - readlink_by_handle\n");
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Check the access by using NFS4 ACL if it exists. Otherwise, use mode. */
fsal_status_t fsal_internal_testAccess(const struct req_op_context *p_context,
				       fsal_accessflags_t access_type,	/* IN */
				       struct attrlist *
				       p_object_attributes/* IN */)
{
	if (!p_object_attributes)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* The root user ignores the mode/uid/gid of the file */
	if (p_context->creds->caller_uid == 0)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	/* Use mode to check access. */
	return fsal_internal_testAccess_no_acl(p_context,
					       FSAL_MODE_MASK(access_type),
					       p_object_attributes);
}

/* Check the access at the file system. It is called when Use_Test_Access
 * = 0.
 */
fsal_status_t fsal_internal_access(int mntfd,	/* IN */
				   const struct req_op_context *p_context,
				   ptfsal_handle_t *p_handle,	/* IN */
				   fsal_accessflags_t access_type,	/* IN */
				   struct attrlist *p_object_attributes)
{				/* IN */
	fsal_status_t status;
/*	mode_t mode = 0; */

	FSI_TRACE(FSI_DEBUG, "FSI - access\n");

	/* sanity checks. */
	if (!p_context || !p_handle)
		return fsalstat(ERR_FSAL_FAULT, 0);

/*	if (IS_FSAL_MODE_MASK_VALID(access_type)) */
/*		mode = FSAL_MODE_MASK(access_type); */

	status =
	    fsal_internal_testAccess(p_context, access_type,
				     p_object_attributes);

	return status;
}

/* Get NFS4 ACL as well as stat. For now, get stat only until NFS4 ACL
 * support is enabled. */
fsal_status_t fsal_get_xstat_by_handle(int dirfd,
				       struct pt_file_handle *p_handle,
				       ptfsal_xstat_t *p_buffxstat)
{

	FSI_TRACE(FSI_DEBUG, "FSI - get_xstat_by_handle\n");

	if (!p_handle || !p_buffxstat)
		return fsalstat(ERR_FSAL_FAULT, 0);

	memset(p_buffxstat, 0, sizeof(ptfsal_xstat_t));

	/* figure out what to return */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Set NFS4 ACL as well as stat. For now, set stat only until NFS4 ACL
 * support is enabled. */
fsal_status_t fsal_set_xstat_by_handle(int dirfd,
				       const struct req_op_context *p_context,
				       struct pt_file_handle *p_handle,
				       int attr_valid, int attr_changed,
				       ptfsal_xstat_t *p_buffxstat)
{
	FSI_TRACE(FSI_DEBUG, "FSI - set_xstat_by_handle\n");

	if (!p_handle || !p_buffxstat)
		return fsalstat(ERR_FSAL_FAULT, 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
fsal_internal_testAccess_no_acl(const struct req_op_context *p_context,
				fsal_accessflags_t access_type,	/* IN */
				struct attrlist
				*p_object_attributes)
{				/* IN */
	fsal_accessflags_t missing_access;
	unsigned int is_grp, i;
	uid_t uid;
	gid_t gid;
	mode_t mode;

	FSI_TRACE(FSI_DEBUG, "FSI - testAccess_no_acl\n");

	/* unsatisfied flags */
	missing_access = access_type;
	if (!missing_access) {
		LogDebug(COMPONENT_FSAL, "Nothing was requested");
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	uid = p_object_attributes->owner;
	gid = p_object_attributes->group;
	mode = p_object_attributes->mode;

	LogDebug(COMPONENT_FSAL, "file Mode=%#o, file uid=%d, file gid= %d",
		 mode, uid, gid);
	LogDebug(COMPONENT_FSAL, "user uid=%d, user gid= %d, access_type=0X%x",
		 p_context->creds->caller_uid, p_context->creds->caller_gid,
		 access_type);

	/* If the uid of the file matches the uid of the user,
	 * then the uid mode bits take precedence. */
	if (p_context->creds->caller_uid == uid) {

		LogDebug(COMPONENT_FSAL, "File belongs to user %d", uid);

		if (mode & S_IRUSR)
			missing_access &= ~FSAL_R_OK;

		if (mode & S_IWUSR)
			missing_access &= ~FSAL_W_OK;

		if (mode & S_IXUSR)
			missing_access &= ~FSAL_X_OK;

		if (missing_access == 0)
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		else {
			LogDebug(COMPONENT_FSAL,
				 "Mode=%#o, Access=0X%x, Rights missing: 0X%x",
				 mode, access_type, missing_access);
			return fsalstat(ERR_FSAL_ACCESS, 0);
		}

	}

	/* Test if the file belongs to user's group. */
	is_grp = (p_context->creds->caller_gid == gid);
	if (is_grp)
		LogDebug(COMPONENT_FSAL, "File belongs to user's group %d",
			 p_context->creds->caller_gid);

	/* Test if file belongs to alt user's groups */
	if (!is_grp) {
		for (i = 0; i < p_context->creds->caller_glen; i++) {
			is_grp = (p_context->creds->caller_garray[i] == gid);
			if (is_grp)
				LogDebug(COMPONENT_FSAL,
					 "File belongs to user's alt group %d",
					 p_context->creds->caller_garray[i]);
			if (is_grp)
				break;
		}
	}
	/* If the gid of the file matches the gid of the user or
	 * one of the alternatve gids of the user, then the uid mode
	 * bits take precedence. */
	if (is_grp) {
		if (mode & S_IRGRP)
			missing_access &= ~FSAL_R_OK;

		if (mode & S_IWGRP)
			missing_access &= ~FSAL_W_OK;

		if (mode & S_IXGRP)
			missing_access &= ~FSAL_X_OK;

		if (missing_access == 0)
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		else
			return fsalstat(ERR_FSAL_ACCESS, 0);

	}

	/* If the user uid is not 0, the uid does not match the file's, and
	 * the user's gids do not match the file's gid, we apply the "other"
	 * mode bits to the user. */
	if (mode & S_IROTH)
		missing_access &= ~FSAL_R_OK;

	if (mode & S_IWOTH)
		missing_access &= ~FSAL_W_OK;

	if (mode & S_IXOTH)
		missing_access &= ~FSAL_X_OK;

	if (missing_access == 0)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	else {
		LogDebug(COMPONENT_FSAL,
			 "Mode=%#o, Access=0X%x, Rights missing: 0X%x", mode,
			 access_type, missing_access);
		return fsalstat(ERR_FSAL_ACCESS, 0);
	}

}

/**
 *  fsal_error_is_info:
 *  Indicates if an FSAL error should be posted as an INFO level debug msg.
 *  \param status(input): The fsal status whom event is to be tested.
 *  \return - TRUE if the error event is to be posted.
 *          - FALSE if the error event is NOT to be posted.
 */
bool_t fsal_error_is_info(fsal_status_t status)
{
	switch (status.major) {
	case ERR_FSAL_PERM:
	case ERR_FSAL_NOT_OPENED:
	case ERR_FSAL_ACCESS:
	case ERR_FSAL_FILE_OPEN:
	case ERR_FSAL_DELAY:
	case ERR_FSAL_NOTEMPTY:
	case ERR_FSAL_DQUOT:
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

/**
 *  fsal_error_is_event:
 *  Indicates if an FSAL error should be posted as an event
 *  \param status(input): The fsal status whom event is to be tested.
 *  \return - TRUE if the error event is to be posted.
 *          - FALSE if the error event is NOT to be posted.
 *
 */
bool_t fsal_error_is_event(fsal_status_t status)
{

	switch (status.major) {

	case ERR_FSAL_IO:
	case ERR_FSAL_STALE:
		return TRUE;

	default:
		return FALSE;
	}
}
