/*
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
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @addtogroup FSAL
 * @{
 */

/**
 * @file fsal_commonlib.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Common functions for and private to FSAL modules.
 *
 * The prime requirement for functions to be here is that they operate only
 * on the public part of the FSAL api and are therefore sharable by all fsal
 * implementations.
 */
#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <os/quota.h>
#include "nlm_list.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_private.h"
#include "fsal_convert.h"

/* fsal_module to fsal_export helpers
 */

/* fsal_attach_export
 * called from the FSAL's create_export method with a reference on the fsal.
 */

int fsal_attach_export(struct fsal_module *fsal_hdl,
		       struct glist_head *obj_link)
{
	int retval = 0;

	pthread_mutex_lock(&fsal_hdl->lock);
	if (fsal_hdl->refs > 0) {
		glist_add(&fsal_hdl->exports, obj_link);
	} else {
		LogCrit(COMPONENT_CONFIG,
			"Attaching export with out holding a reference!. hdl= = 0x%p",
			fsal_hdl);
		retval = EINVAL;
	}
	pthread_mutex_unlock(&fsal_hdl->lock);
	return retval;
}

/* fsal_detach_export
 * called by an export when it is releasing itself.
 * does not require a reference to be taken.  The list has
 * kept the fsal "busy".
 */

void fsal_detach_export(struct fsal_module *fsal_hdl,
			struct glist_head *obj_link)
{
	pthread_mutex_lock(&fsal_hdl->lock);
	glist_del(obj_link);
	pthread_mutex_unlock(&fsal_hdl->lock);
}

/* fsal_export to fsal_obj_handle helpers
 */

static void fsal_attach_handle(struct fsal_module *fsal,
			       struct glist_head *obj_link)
{
	pthread_mutex_lock(&fsal->lock);
	glist_add(&fsal->handles, obj_link);
	pthread_mutex_unlock(&fsal->lock);
}

static void fsal_detach_handle(struct fsal_module *fsal,
			       struct glist_head *obj_link)
{
	pthread_mutex_lock(&fsal->lock);
	glist_del(obj_link);
	pthread_mutex_unlock(&fsal->lock);
}

int fsal_export_init(struct fsal_export *exp, struct exportlist *exp_entry)
{
	pthread_mutexattr_t attrs;

	exp->ops = gsh_malloc(sizeof(struct export_ops));
	if (exp->ops == NULL)
		goto errout;
	memcpy(exp->ops, &def_export_ops, sizeof(struct export_ops));

	exp->obj_ops = gsh_malloc(sizeof(struct fsal_obj_ops));
	if (exp->obj_ops == NULL)
		goto errout;
	memcpy(exp->obj_ops, &def_handle_ops, sizeof(struct fsal_obj_ops));

	exp->ds_ops = gsh_malloc(sizeof(struct fsal_ds_ops));
	if (exp->ds_ops == NULL)
		goto errout;
	memcpy(exp->ds_ops, &def_ds_ops, sizeof(struct fsal_ds_ops));

	pthread_mutexattr_init(&attrs);
#if defined(__linux__)
	pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
#endif
	pthread_mutex_init(&exp->lock, &attrs);

	exp->refs = 1;		/* we exit with a reference held */
	return 0;

 errout:
	if (exp->ops)
		gsh_free(exp->ops);
	if (exp->obj_ops)
		gsh_free(exp->obj_ops);
	return ENOMEM;
}

/**
 * @brief Free export ops vectors
 *
 * Free the memory allocated by init_export_ops. Poison pointers.
 *
 * @param[in] exp_hdl Export handle
 *
 */

void free_export_ops(struct fsal_export *exp_hdl)
{
	if (exp_hdl->ops) {
		gsh_free(exp_hdl->ops);
		exp_hdl->ops = NULL;
	}
	if (exp_hdl->obj_ops) {
		gsh_free(exp_hdl->obj_ops);
		exp_hdl->obj_ops = NULL;
	}
	if (exp_hdl->ds_ops) {
		gsh_free(exp_hdl->ds_ops);
		exp_hdl->ds_ops = NULL;
	}
}

void fsal_obj_handle_init(struct fsal_obj_handle *obj, struct fsal_export *exp,
			  object_file_type_t type)
{
	pthread_mutexattr_t attrs;

	obj->refs = 1;		/* we start out with a reference */
	obj->ops = exp->obj_ops;
	obj->fsal = exp->fsal;
	obj->type = type;
	pthread_mutexattr_init(&attrs);
#if defined(__linux__)
	pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
#endif
	pthread_mutex_init(&obj->lock, &attrs);

	fsal_attach_handle(exp->fsal, &obj->handles);
}

int fsal_obj_handle_uninit(struct fsal_obj_handle *obj)
{
	pthread_mutex_lock(&obj->lock);
	obj->refs--;	/* subtract the reference when we were created */
	if (obj->refs != 0) {
		pthread_mutex_unlock(&obj->lock);
		return EBUSY;
	}

	pthread_mutex_unlock(&obj->lock);
	pthread_mutex_destroy(&obj->lock);

	fsal_detach_handle(obj->fsal, &obj->handles);

	obj->ops = NULL;	/*poison myself */
	obj->fsal = NULL;

	return 0;
}

void fsal_attach_ds(struct fsal_module *fsal, struct glist_head *ds_link)
{
	pthread_mutex_lock(&fsal->lock);
	glist_add(&fsal->ds_handles, ds_link);
	pthread_mutex_unlock(&fsal->lock);
}

void fsal_detach_ds(struct fsal_module *fsal, struct glist_head *ds_link)
{
	pthread_mutex_lock(&fsal->lock);
	glist_del(ds_link);
	pthread_mutex_unlock(&fsal->lock);
}

void fsal_ds_handle_init(struct fsal_ds_handle *ds, struct fsal_ds_ops *ops,
			 struct fsal_module *fsal)
{
	pthread_mutexattr_t attrs;

	ds->refs = 1;		/* we start out with a reference */
	ds->ops = ops;
	ds->fsal = fsal;
	pthread_mutexattr_init(&attrs);
#if defined(__linux__)
	pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
#endif
	pthread_mutex_init(&ds->lock, &attrs);

	fsal_attach_ds(fsal, &ds->ds_handles);
}

int fsal_ds_handle_uninit(struct fsal_ds_handle *ds)
{
	pthread_mutex_lock(&ds->lock);
	if (ds->refs) {
		pthread_mutex_unlock(&ds->lock);
		return EINVAL;
	}

	pthread_mutex_unlock(&ds->lock);
	pthread_mutex_destroy(&ds->lock);

	fsal_detach_ds(ds->fsal, &ds->ds_handles);

	ds->ops = NULL;		/*poison myself */
	ds->fsal = NULL;

	return 0;
}

/**
 * @brief FSAL error code to error message
 *
 * @param[in] fsal_err Error code
 *
 * @return Error message, empty string if not found.
 */

const char *msg_fsal_err(fsal_errors_t fsal_err)
{
	switch (fsal_err) {
	case ERR_FSAL_NO_ERROR:
		return "No error";
	case ERR_FSAL_PERM:
		return "Forbidden action";
	case ERR_FSAL_NOENT:
		return "No such file or directory";
	case ERR_FSAL_IO:
		return "I/O error";
	case ERR_FSAL_NXIO:
		return "No such device or address";
	case ERR_FSAL_NOMEM:
		return "Not enough memory";
	case ERR_FSAL_ACCESS:
		return "Permission denied";
	case ERR_FSAL_FAULT:
		return "Bad address";
	case ERR_FSAL_EXIST:
		return "This object already exists";
	case ERR_FSAL_XDEV:
		return "This operation can't cross filesystems";
	case ERR_FSAL_NOTDIR:
		return "This object is not a directory";
	case ERR_FSAL_ISDIR:
		return "Directory used in a nondirectory operation";
	case ERR_FSAL_INVAL:
		return "Invalid object type";
	case ERR_FSAL_FBIG:
		return "File exceeds max file size";
	case ERR_FSAL_NOSPC:
		return "No space left on filesystem";
	case ERR_FSAL_ROFS:
		return "Read-only filesystem";
	case ERR_FSAL_MLINK:
		return "Too many hard links";
	case ERR_FSAL_DQUOT:
		return "Quota exceeded";
	case ERR_FSAL_NAMETOOLONG:
		return "Max name length exceeded";
	case ERR_FSAL_NOTEMPTY:
		return "The directory is not empty";
	case ERR_FSAL_STALE:
		return "The file no longer exists";
	case ERR_FSAL_BADHANDLE:
		return "Illegal filehandle";
	case ERR_FSAL_BADCOOKIE:
		return "Invalid cookie";
	case ERR_FSAL_NOTSUPP:
		return "Operation not supported";
	case ERR_FSAL_TOOSMALL:
		return "Output buffer too small";
	case ERR_FSAL_SERVERFAULT:
		return "Undefined server error";
	case ERR_FSAL_BADTYPE:
		return "Invalid type for create operation";
	case ERR_FSAL_DELAY:
		return "File busy, retry";
	case ERR_FSAL_FHEXPIRED:
		return "Filehandle expired";
	case ERR_FSAL_SYMLINK:
		return "This is a symbolic link, should be file/directory";
	case ERR_FSAL_ATTRNOTSUPP:
		return "Attribute not supported";
	case ERR_FSAL_NOT_INIT:
		return "Filesystem not initialized";
	case ERR_FSAL_ALREADY_INIT:
		return "Filesystem already initialised";
	case ERR_FSAL_BAD_INIT:
		return "Filesystem initialisation error";
	case ERR_FSAL_SEC:
		return "Security context error";
	case ERR_FSAL_NO_QUOTA:
		return "No Quota available";
	case ERR_FSAL_NOT_OPENED:
		return "File/directory not opened";
	case ERR_FSAL_DEADLOCK:
		return "Deadlock";
	case ERR_FSAL_OVERFLOW:
		return "Overflow";
	case ERR_FSAL_INTERRUPT:
		return "Operation Interrupted";
	case ERR_FSAL_BLOCKED:
		return "Lock Blocked";
	case ERR_FSAL_SHARE_DENIED:
		return "Share Denied";
	case ERR_FSAL_TIMEOUT:
		return "Timeout";
	case ERR_FSAL_FILE_OPEN:
		return "File Open";
	case ERR_FSAL_UNION_NOTSUPP:
		return "Union Not Supported";
	case ERR_FSAL_IN_GRACE:
		return "Server in Grace";
	}

	return "Unknown FSAL error";
}

/**
 * @brief Dump and fsal_staticfsinfo_t to a log
 *
 * This is used for debugging
 *
 * @param[in] info The info to dump
 */
void display_fsinfo(struct fsal_staticfsinfo_t *info)
{
	LogDebug(COMPONENT_FSAL, "FileSystem info: {");
	LogDebug(COMPONENT_FSAL, "  maxfilesize  = %" PRIX64 "    ",
		 (uint64_t) info->maxfilesize);
	LogDebug(COMPONENT_FSAL, "  maxlink  = %" PRIu32, info->maxlink);
	LogDebug(COMPONENT_FSAL, "  maxnamelen  = %" PRIu32, info->maxnamelen);
	LogDebug(COMPONENT_FSAL, "  maxpathlen  = %" PRIu32, info->maxpathlen);
	LogDebug(COMPONENT_FSAL, "  no_trunc  = %d ", info->no_trunc);
	LogDebug(COMPONENT_FSAL, "  chown_restricted  = %d ",
		 info->chown_restricted);
	LogDebug(COMPONENT_FSAL, "  case_insensitive  = %d ",
		 info->case_insensitive);
	LogDebug(COMPONENT_FSAL, "  case_preserving  = %d ",
		 info->case_preserving);
	LogDebug(COMPONENT_FSAL, "  link_support  = %d  ", info->link_support);
	LogDebug(COMPONENT_FSAL, "  symlink_support  = %d  ",
		 info->symlink_support);
	LogDebug(COMPONENT_FSAL, "  lock_support  = %d  ", info->lock_support);
	LogDebug(COMPONENT_FSAL, "  lock_support_owner  = %d  ",
		 info->lock_support_owner);
	LogDebug(COMPONENT_FSAL, "  lock_support_async_block  = %d  ",
		 info->lock_support_async_block);
	LogDebug(COMPONENT_FSAL, "  named_attr  = %d  ", info->named_attr);
	LogDebug(COMPONENT_FSAL, "  unique_handles  = %d  ",
		 info->unique_handles);
	LogDebug(COMPONENT_FSAL, "  acl_support  = %hu  ", info->acl_support);
	LogDebug(COMPONENT_FSAL, "  cansettime  = %d  ", info->cansettime);
	LogDebug(COMPONENT_FSAL, "  homogenous  = %d  ", info->homogenous);
	LogDebug(COMPONENT_FSAL, "  supported_attrs  = %" PRIX64,
		 info->supported_attrs);
	LogDebug(COMPONENT_FSAL, "  maxread  = %" PRIu32, info->maxread);
	LogDebug(COMPONENT_FSAL, "  maxwrite  = %" PRIu32, info->maxwrite);
	LogDebug(COMPONENT_FSAL, "  umask  = %X ", info->umask);
	LogDebug(COMPONENT_FSAL, "  auth_exportpath_xdev  = %d  ",
		 info->auth_exportpath_xdev);
	LogDebug(COMPONENT_FSAL, "  xattr_access_rights = %#o ",
		 info->xattr_access_rights);
	LogDebug(COMPONENT_FSAL, "  accesscheck_support  = %d  ",
		 info->accesscheck_support);
	LogDebug(COMPONENT_FSAL, "  share_support  = %d  ",
		 info->share_support);
	LogDebug(COMPONENT_FSAL, "  share_support_owner  = %d  ",
		 info->share_support_owner);
	LogDebug(COMPONENT_FSAL, "  delegations = %d  ",
		 info->delegations);
	LogDebug(COMPONENT_FSAL, "  pnfs_file = %d  ",
		 info->pnfs_file);
	LogDebug(COMPONENT_FSAL, "  fsal_trace = %d  ",
		 info->fsal_trace);
	LogDebug(COMPONENT_FSAL, "}");
}

int open_dir_by_path_walk(int first_fd, const char *path, struct stat *stat)
{
	char *name, *rest, *p;
	int fd = first_fd, len, rc, err;

	/* Get length of the path */
	len = strlen(path);

	/* Strip terminating '/' by shrinking length */
	while (path[len-1] == '/' && len > 1)
		len--;

	/* Allocate space for duplicate */
	name = alloca(len + 1);

	if (name == NULL) {
		LogCrit(COMPONENT_FSAL,
			"No memory for path duplicate of %s",
			path);
		return -ENOMEM;
	}

	/* Copy the string */
	memcpy(name, path, len);
	/* Terminate it */

	name[len] = '\0';

	/* Determine if this is a relative path off some directory
	 * or an absolute path. If absolute path, open root dir.
	 */
	if (first_fd == -1) {
		if (name[0] != '/') {
			LogInfo(COMPONENT_FSAL,
				"Absolute path %s must start with '/'",
				path);
			return -EINVAL;
		}
		rest = name + 1;
		fd = open("/", O_RDONLY | O_NOFOLLOW);
	} else {
		rest = name;
		fd = dup(first_fd);
	}

	if (fd == -1) {
		err = errno;
		LogCrit(COMPONENT_FSAL,
			"Failed initial directory open for path %s with %s",
			path, strerror(err));
		return -err;
	}

	while (rest[0] != '\0') {
		/* Find the end of this path element */
		p = index(rest, '/');

		/* NUL terminate element (if not at end of string */
		if (p != NULL)
			*p = '\0';

		/* Skip extra '/' */
		if (rest[0] == '\0') {
			rest++;
			continue;
		}

		/* Disallow .. elements... */
		if (strcmp(rest, "..") == 0) {
			close(fd);
			LogInfo(COMPONENT_FSAL,
				"Failed due to '..' element in path %s",
				path);
			return -EACCES;
		}

		/* Open the next directory in the path */
		rc = openat(fd, rest, O_RDONLY | O_NOFOLLOW);
		err = errno;

		close(fd);

		if (rc == -1) {
			LogDebug(COMPONENT_FSAL,
				 "openat(%s) in path %s failed with %s",
				 rest, path, strerror(err));
			return -err;
		}

		fd = rc;

		/* Done, break out */
		if (p == NULL)
			break;

		/* Skip the '/' */
		rest = p+1;
	}

	rc = fstat(fd, stat);
	err = errno;

	if (rc == -1) {
		close(fd);
		LogDebug(COMPONENT_FSAL,
			 "fstat %s failed with %s",
			 path, strerror(err));
		return -err;
	}

	if (!S_ISDIR(stat->st_mode)) {
		close(fd);
		LogInfo(COMPONENT_FSAL,
			"Path %s is not a directory",
			path);
		return -ENOTDIR;
	}

	return fd;
}

/** @} */
