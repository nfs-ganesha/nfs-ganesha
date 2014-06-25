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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 *
 * \file    fsal_attrs.c
 * \brief   Attributes functions.
 *
 */
#include "config.h"

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <sys/time.h>
#include "export_mgr.h"

#ifdef _USE_NFS4_ACL
extern fsal_status_t fsal_acl_2_gpfs_acl(fsal_acl_t *p_fsalacl,
					 gpfsfsal_xstat_t *p_buffxstat);
#endif				/* _USE_NFS4_ACL */

/**
 * GPFSFSAL_getattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t GPFSFSAL_getattrs(struct fsal_export *export,	/* IN */
				struct gpfs_filesystem *gpfs_fs, /* IN */
				const struct req_op_context *p_context,	/* IN */
				struct gpfs_file_handle *p_filehandle,	/* IN */
				struct attrlist *p_object_attributes)
{				/* IN/OUT */
	fsal_status_t st;
	gpfsfsal_xstat_t buffxstat;
	bool expire;
	uint32_t expire_time_attr = 0;	/*< Expiration time for attributes. */

	/* Initialize fsal_fsid to 0.0 in case older GPFS */
	buffxstat.fsal_fsid.major = 0;
	buffxstat.fsal_fsid.minor = 0;

	expire = p_context->export->expire_time_attr > 0;

	st = fsal_get_xstat_by_handle(gpfs_fs->root_fd, p_filehandle,
				      &buffxstat, &expire_time_attr, expire);
	if (FSAL_IS_ERROR(st))
		return st;

	/* convert attributes */
	if (expire_time_attr != 0)
		p_object_attributes->expire_time_attr = expire_time_attr;

	/* Assume if fsid = 0.0, then old GPFS didn't fill it in, in that
	 * case, fill in from the object's filesystem.
	 */
	if (buffxstat.fsal_fsid.major == 0 && buffxstat.fsal_fsid.minor == 0)
		buffxstat.fsal_fsid = gpfs_fs->fs->fsid;

	st = gpfsfsal_xstat_2_fsal_attributes(&buffxstat, p_object_attributes);
	if (FSAL_IS_ERROR(st)) {
		FSAL_CLEAR_MASK(p_object_attributes->mask);
		FSAL_SET_MASK(p_object_attributes->mask, ATTR_RDATTR_ERR);
		return st;
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * GPFSFSAL_statfs:
 * Get fs attributes for the object specified by its filehandle.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t GPFSFSAL_statfs(int mountdirfd,			/* IN */
			      struct fsal_obj_handle *obj_hdl,   /* IN */
			      struct statfs *buf)                /* OUT */
{
	int rc;
	struct statfs_arg sarg;
	struct gpfs_fsal_obj_handle *myself;
	int errsv;

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);

	sarg.handle = myself->handle;
	sarg.mountdirfd = mountdirfd;
	sarg.buf = buf;

	rc = gpfs_ganesha(OPENHANDLE_STATFS_BY_FH, &sarg);
	errsv = errno;

	LogFullDebug(COMPONENT_FSAL,
		     "OPENHANDLE_STATFS_BY_FH returned: rc %d", rc);

	if (rc < 0) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * GPFSFSAL_setattrs:
 * Set attributes for the object specified by its filehandle.
 *
 * \param dir_hdl (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param p_attrib_set (mandatory input):
 *        The attributes to be set for the object.
 *        It defines the attributes that the caller
 *        wants to set and their values.
 * \param p_object_attributes (optionnal input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t GPFSFSAL_setattrs(struct fsal_obj_handle *dir_hdl,	/* IN */
				const struct req_op_context *p_context,	/* IN */
				struct attrlist *p_object_attributes)
{				/* IN */
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *myself;
	gpfsfsal_xstat_t buffxstat;
	struct gpfs_filesystem *gpfs_fs;

	/* Indicate if stat or acl or both should be changed. */
	int attr_valid = 0;

	/* Indiate which attribute in stat should be changed. */
	int attr_changed = 0;

	/* sanity checks.
	 * note : object_attributes is optional.
	 */
	if (!dir_hdl || !p_context || !p_object_attributes)
		return fsalstat(ERR_FSAL_FAULT, 0);

	myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = dir_hdl->fs->private;

	/* First, check that FSAL attributes changes are allowed. */

	/* Is it allowed to change times ? */

	if (!p_context->fsal_export->ops->
			fs_supports(p_context->fsal_export,
				    fso_cansettime)) {
		if (p_object_attributes->
		    mask & (ATTR_ATIME | ATTR_CREATION | ATTR_CTIME | ATTR_MTIME
			    | ATTR_MTIME_SERVER | ATTR_ATIME_SERVER)) {
			/* handled as an unsettable attribute. */
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
	}

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR_MODE)) {
		p_object_attributes->mode &=
		    ~p_context->fsal_export->ops->
			fs_umask(p_context->fsal_export);
	}

  /**************
   *  TRUNCATE  *
   **************/

	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR_SIZE)) {
		attr_changed |= XATTR_SIZE;
		/* Fill wanted mode. */
		buffxstat.buffstat.st_size = p_object_attributes->filesize;
		LogDebug(COMPONENT_FSAL, "current size = %llu, new size = %llu",
			 (unsigned long long)dir_hdl->attributes.filesize,
			 (unsigned long long)buffxstat.buffstat.st_size);
	}

  /*******************
   *  SPACE RESERVED *
   ************)******/

	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR4_SPACE_RESERVED)) {
		attr_changed |= XATTR_SPACE_RESERVED;
		/* Fill wanted mode. */
		buffxstat.buffstat.st_size = p_object_attributes->filesize;
		LogDebug(COMPONENT_FSAL, "current size = %llu, new size = %llu",
			 (unsigned long long)dir_hdl->attributes.filesize,
			 (unsigned long long)buffxstat.buffstat.st_size);
	}

  /***********
   *  CHMOD  *
   ***********/
	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR_MODE)) {

		/* The POSIX chmod call don't affect the symlink object, but
		 * the entry it points to. So we must ignore it.
		 */
		if (dir_hdl->type != SYMBOLIC_LINK) {
			attr_changed |= XATTR_MODE;

			/* Fill wanted mode. */
			buffxstat.buffstat.st_mode =
			    fsal2unix_mode(p_object_attributes->mode);
			LogDebug(COMPONENT_FSAL,
				 "current mode = %o, new mode = %o",
				 fsal2unix_mode(dir_hdl->attributes.mode),
				 buffxstat.buffstat.st_mode);

		}

	}

  /***********
   *  CHOWN  *
   ***********/

	/* Fill wanted owner. */
	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR_OWNER)) {
		attr_changed |= XATTR_UID;
		buffxstat.buffstat.st_uid = (int)p_object_attributes->owner;
		LogDebug(COMPONENT_FSAL, "current uid = %ld, new uid = %d",
			 dir_hdl->attributes.owner, buffxstat.buffstat.st_uid);
	}

	/* Fill wanted group. */
	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR_GROUP)) {
		attr_changed |= XATTR_GID;
		buffxstat.buffstat.st_gid = (int)p_object_attributes->group;
		LogDebug(COMPONENT_FSAL, "current gid = %ld, new gid = %d",
			 dir_hdl->attributes.group, buffxstat.buffstat.st_gid);
	}

  /***********
   *  UTIME  *
   ***********/

	/* Fill wanted atime. */
	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR_ATIME)) {
		attr_changed |= XATTR_ATIME;
		buffxstat.buffstat.st_atime =
		    (time_t) p_object_attributes->atime.tv_sec;
		buffxstat.buffstat.st_atim.tv_nsec =
		    p_object_attributes->atime.tv_nsec;
		LogDebug(COMPONENT_FSAL, "current atime = %lu, new atime = %lu",
			 (unsigned long)dir_hdl->attributes.atime.tv_sec,
			 (unsigned long)buffxstat.buffstat.st_atime);
	}

	/* Fill wanted mtime. */
	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR_MTIME)) {
		attr_changed |= XATTR_MTIME;
		buffxstat.buffstat.st_mtime =
		    (time_t) p_object_attributes->mtime.tv_sec;
		buffxstat.buffstat.st_mtim.tv_nsec =
		    p_object_attributes->mtime.tv_nsec;
		LogDebug(COMPONENT_FSAL, "current mtime = %lu, new mtime = %lu",
			 (unsigned long)dir_hdl->attributes.mtime.tv_sec,
			 (unsigned long)buffxstat.buffstat.st_mtime);
	}
	/* Asking to set atime to NOW */
	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR_ATIME_SERVER)) {
		attr_changed |= XATTR_ATIME | XATTR_ATIME_NOW;
		LogDebug(COMPONENT_FSAL, "current atime = %lu, new atime = NOW",
			 (unsigned long)dir_hdl->attributes.atime.tv_sec);
	}
	/* Asking to set atime to NOW */
	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR_MTIME_SERVER)) {
		attr_changed |= XATTR_MTIME | XATTR_MTIME_NOW;
		LogDebug(COMPONENT_FSAL, "current mtime = %lu, new mtime = NOW",
			 (unsigned long)dir_hdl->attributes.atime.tv_sec);
	}

	/* If any stat changed, indicate that */
	if (attr_changed != 0)
		attr_valid |= XATTR_STAT;

#ifdef _USE_NFS4_ACL
   /***********
   *  ACL  *
   ***********/

	if (FSAL_TEST_MASK(p_object_attributes->mask, ATTR_ACL)) {
		if (p_object_attributes->acl) {
			attr_valid |= XATTR_ACL;
			LogDebug(COMPONENT_FSAL, "setattr acl = %p",
				 p_object_attributes->acl);

			/* Convert FSAL ACL to GPFS NFS4 ACL and fill buffer. */
			status =
			    fsal_acl_2_gpfs_acl(p_object_attributes->acl,
						&buffxstat);

			if (FSAL_IS_ERROR(status))
				return status;
		} else {
			LogCrit(COMPONENT_FSAL, "setattr acl is NULL");
			return fsalstat(ERR_FSAL_FAULT, 0);
		}
	}
#endif				/* _USE_NFS4_ACL */

	/* If there is any change in stat or acl or both, send it down to fs. */
	if (attr_valid != 0) {
		status = fsal_set_xstat_by_handle(gpfs_fs->root_fd, p_context,
						  myself->handle, attr_valid,
						  attr_changed, &buffxstat);

		if (FSAL_IS_ERROR(status))
			return status;
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
