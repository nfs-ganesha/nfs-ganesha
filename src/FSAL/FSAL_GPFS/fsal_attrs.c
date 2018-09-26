/** @file fsal_attrs.c
 *  @brief GPFS FSAL attribute functions
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
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

extern fsal_status_t
fsal_acl_2_gpfs_acl(struct fsal_obj_handle *, fsal_acl_t *, gpfsfsal_xstat_t *,
		    gpfs_acl_t *acl_buf, unsigned int acl_buflen);

/**
 *  @brief Get fs_locations attribute for the object specified by its
 *         filehandle.
 *
 *  @param export FSAL export
 *  @param gpfs_fs GPFS filesystem
 *  @param op_ctx Request op context
 *  @param gpfs_fh GPFS file handle
 *  @param attrs Object attributes (fs_locations is initialized
 *                                  on a successful return)
 *  @return FSAL status
 *
 */
fsal_status_t
GPFSFSAL_fs_loc(struct fsal_export *export, struct gpfs_filesystem *gpfs_fs,
		const struct req_op_context *op_ctx,
		struct gpfs_file_handle *gpfs_fh, struct attrlist *attrs)
{
	char root[MAXPATHLEN];
	char path[MAXPATHLEN];
	char server[MAXHOSTNAMELEN];
	int errsv, rc;
	struct fs_loc_arg loc_arg;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	loc_arg.fs_root = root;
	loc_arg.fs_root_len = sizeof(root);
	loc_arg.fs_path = path;
	loc_arg.fs_path_len = sizeof(path);
	loc_arg.fs_server = server;
	loc_arg.fs_server_len = sizeof(server);
	loc_arg.mountdirfd = export_fd;
	loc_arg.handle = gpfs_fh;

	rc = gpfs_ganesha(OPENHANDLE_FS_LOCATIONS, &loc_arg);
	errsv = errno;
	LogDebug(COMPONENT_FSAL,
		 "gpfs_ganesha: FS_LOCATIONS returned, rc %d errsv %d",
		 rc, errsv);

	if (rc)
		return fsalstat(ERR_FSAL_ATTRNOTSUPP, 0);

	attrs->fs_locations = nfs4_fs_locations_new(root, path, 1);
	attrs->fs_locations->nservers = 1;
	attrs->fs_locations->server[0].utf8string_len = strlen(server);
	attrs->fs_locations->server[0].utf8string_val =
		gsh_memdup(server, strlen(server));


	LogDebug(COMPONENT_FSAL,
		 "gpfs_ganesha: FS_LOCATIONS root=%s path=%s server=%s",
		 root, path, server);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  @brief Get attributes for the object specified by its filehandle.
 *
 *  @param export FSAL export
 *  @param gpfs_fs GPFS filesystem
 *  @param op_ctx Request op context
 *  @param gpfs_fh GPFS file handle
 *  @param obj_attr Object attributes
 *  @return FSAL status
 */
fsal_status_t
GPFSFSAL_getattrs(struct fsal_export *export, struct gpfs_filesystem *gpfs_fs,
		  const struct req_op_context *op_ctx,
		  struct gpfs_file_handle *gpfs_fh, struct attrlist *obj_attr)
{
	fsal_status_t st;
	gpfsfsal_xstat_t buffxstat;
	bool expire;
	uint32_t expire_time_attr = 0;	/*< Expiration time for attributes. */
	struct gpfs_fsal_export *gpfs_export;
	gpfs_acl_t *acl_buf;
	unsigned int acl_buflen;
	bool use_acl;
	int retry;
	struct gpfs_fsal_export *exp = container_of(op_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	/* Initialize fsal_fsid to 0.0 in case older GPFS */
	buffxstat.fsal_fsid.major = 0;
	buffxstat.fsal_fsid.minor = 0;

	expire = op_ctx->export_perms->expire_time_attr > 0;

	gpfs_export = container_of(export, struct gpfs_fsal_export, export);

	/* Let us make the first request using the acl buffer that
	 * is part of buffxstat itself. If that is not sufficient,
	 * we allocate from heap and retry.
	 */
	use_acl = obj_attr->request_mask & ATTR_ACL;
	for (retry = 0; retry < GPFS_ACL_MAX_RETRY; retry++) {
		switch (retry) {
		case 0: /* first attempt */
			acl_buf = (gpfs_acl_t *)buffxstat.buffacl;
			acl_buflen = GPFS_ACL_BUF_SIZE;
			break;
		case 1: /* first retry, don't free the old stack buffer */
			acl_buflen = acl_buf->acl_len;
			acl_buf = gsh_malloc(acl_buflen);
			break;
		default: /* second or later retry, free the old heap buffer */
			acl_buflen = acl_buf->acl_len;
			gsh_free(acl_buf);
			acl_buf = gsh_malloc(acl_buflen);
			break;
		}

		st = fsal_get_xstat_by_handle(export_fd, gpfs_fh,
				&buffxstat, acl_buf, acl_buflen,
				&expire_time_attr, expire, use_acl);

		if (FSAL_IS_ERROR(st) || !use_acl ||
				acl_buflen >= acl_buf->acl_len)
			break;
	}

	if (retry == GPFS_ACL_MAX_RETRY) { /* make up an error */
		LogCrit(COMPONENT_FSAL, "unable to get ACLs");
		st = fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	if (FSAL_IS_ERROR(st))
		goto error;

	/* convert attributes */
	if (expire_time_attr != 0)
		obj_attr->expire_time_attr = expire_time_attr;

	/* Assume if fsid = 0.0, then old GPFS didn't fill it in, in that
	 * case, fill in from the object's filesystem.
	 */
	if (buffxstat.fsal_fsid.major == 0 && buffxstat.fsal_fsid.minor == 0)
		buffxstat.fsal_fsid = gpfs_fs->fs->fsid;

	st = gpfsfsal_xstat_2_fsal_attributes(&buffxstat, obj_attr, acl_buf,
					      gpfs_export->use_acl);

error:
	if (FSAL_IS_ERROR(st)) {
		if (obj_attr->request_mask & ATTR_RDATTR_ERR) {
			/* Caller asked for error to be visible. */
			obj_attr->valid_mask = ATTR_RDATTR_ERR;
		}
	}

	/* Free acl buffer if we allocated on heap */
	if (acl_buflen != GPFS_ACL_BUF_SIZE) {
		assert(acl_buf != (gpfs_acl_t *)buffxstat.buffacl);
		gsh_free(acl_buf);
	}

	return st;
}

/**
 *  @brief Get fs attributes for the object specified by its filehandle.
 *
 *  @param mountdirfd Mounted filesystem
 *  @param obj_hdl Object handle
 *  @param buf reference to statfs structure
 *  @return FSAL status
 *
 */
fsal_status_t
GPFSFSAL_statfs(int mountdirfd, struct fsal_obj_handle *obj_hdl,
		struct statfs *buf)
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
 *  @brief Set attributes for the object specified by its filehandle.
 *
 *  @param dir_hdl The handle of the object to get parameters.
 *  @param ro_ctx Authentication context for the operation (user,...).
 *  @param obj_attr The post operation attributes for the object.
 *                 As input, it defines the attributes that the caller
 *                 wants to retrieve (by positioning flags into this structure)
 *                 and the output is built considering this input
 *                 (it fills the structure according to the flags it contains).
 *                 May be NULL.
 *  @return FSAL status
 */
fsal_status_t
GPFSFSAL_setattrs(struct fsal_obj_handle *dir_hdl,
		  const struct req_op_context *ro_ctx,
		  struct attrlist *obj_attr)
{
	fsal_status_t status;
	struct gpfs_fsal_obj_handle *myself;
	gpfsfsal_xstat_t buffxstat;
	struct gpfs_fsal_export *gpfs_export;
	gpfs_acl_t *acl_buf = NULL;
	unsigned int acl_buflen = 0;
	bool use_acl;
	struct gpfs_fsal_export *exp = container_of(ro_ctx->fsal_export,
					struct gpfs_fsal_export, export);
	int export_fd = exp->export_fd;

	/* Indicate if stat or acl or both should be changed. */
	int attr_valid = 0;

	/* Indicate which attribute in stat should be changed. */
	int attr_changed = 0;

	myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_export = container_of(ro_ctx->fsal_export,
				   struct gpfs_fsal_export, export);
	use_acl = gpfs_export->use_acl;

	/* First, check that FSAL attributes changes are allowed. */

	/* Is it allowed to change times ? */

	if (!ro_ctx->fsal_export->exp_ops.fs_supports(ro_ctx->fsal_export,
						      fso_cansettime)) {
		if (obj_attr->valid_mask &
			(ATTR_ATIME | ATTR_CREATION | ATTR_CTIME | ATTR_MTIME
			    | ATTR_MTIME_SERVER | ATTR_ATIME_SERVER)) {
			/* handled as an unsettable attribute. */
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
	}

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(obj_attr->valid_mask, ATTR_MODE)) {
		obj_attr->mode &=
		    ~ro_ctx->fsal_export->exp_ops.fs_umask(ro_ctx->fsal_export);
	}

  /**************
   *  TRUNCATE  *
   **************/

	if (FSAL_TEST_MASK(obj_attr->valid_mask, ATTR_SIZE)) {
		attr_changed |= XATTR_SIZE;
		/* Fill wanted size. */
		buffxstat.buffstat.st_size = obj_attr->filesize;
		LogDebug(COMPONENT_FSAL, "new size = %llu",
			 (unsigned long long)buffxstat.buffstat.st_size);
	}

  /*******************
   *  SPACE RESERVED *
   ************)******/

	if (FSAL_TEST_MASK(obj_attr->valid_mask, ATTR4_SPACE_RESERVED)) {
		attr_changed |= XATTR_SPACE_RESERVED;
		/* Fill wanted space. */
		buffxstat.buffstat.st_size = obj_attr->filesize;
		LogDebug(COMPONENT_FSAL, "new size = %llu",
			 (unsigned long long)buffxstat.buffstat.st_size);
	}

  /***********
   *  CHMOD  *
   ***********/
	if (FSAL_TEST_MASK(obj_attr->valid_mask, ATTR_MODE)) {

		/* The POSIX chmod call don't affect the symlink object, but
		 * the entry it points to. So we must ignore it.
		 */
		if (dir_hdl->type != SYMBOLIC_LINK) {
			attr_changed |= XATTR_MODE;

			/* Fill wanted mode. */
			buffxstat.buffstat.st_mode =
			    fsal2unix_mode(obj_attr->mode);
			LogDebug(COMPONENT_FSAL,
				 "new mode = %o",
				 buffxstat.buffstat.st_mode);

		}

	}

  /***********
   *  CHOWN  *
   ***********/

	/* Fill wanted owner. */
	if (FSAL_TEST_MASK(obj_attr->valid_mask, ATTR_OWNER)) {
		attr_changed |= XATTR_UID;
		buffxstat.buffstat.st_uid = (int)obj_attr->owner;
		LogDebug(COMPONENT_FSAL,
			"new uid = %d",
			buffxstat.buffstat.st_uid);
	}

	/* Fill wanted group. */
	if (FSAL_TEST_MASK(obj_attr->valid_mask, ATTR_GROUP)) {
		attr_changed |= XATTR_GID;
		buffxstat.buffstat.st_gid = (int)obj_attr->group;
		LogDebug(COMPONENT_FSAL,
			"new gid = %d",
			buffxstat.buffstat.st_gid);
	}

  /***********
   *  UTIME  *
   ***********/

	/* Fill wanted atime. */
	if (FSAL_TEST_MASK(obj_attr->valid_mask, ATTR_ATIME)) {
		attr_changed |= XATTR_ATIME;
		buffxstat.buffstat.st_atime = (time_t) obj_attr->atime.tv_sec;
		buffxstat.buffstat.st_atim.tv_nsec = obj_attr->atime.tv_nsec;
		LogDebug(COMPONENT_FSAL, "new atime = %lu",
			 (unsigned long)buffxstat.buffstat.st_atime);
	}

	/* Fill wanted mtime. */
	if (FSAL_TEST_MASK(obj_attr->valid_mask, ATTR_MTIME)) {
		attr_changed |= XATTR_MTIME;
		buffxstat.buffstat.st_mtime = (time_t) obj_attr->mtime.tv_sec;
		buffxstat.buffstat.st_mtim.tv_nsec = obj_attr->mtime.tv_nsec;
		LogDebug(COMPONENT_FSAL, "new mtime = %lu",
			 (unsigned long)buffxstat.buffstat.st_mtime);
	}
	/* Asking to set atime to NOW */
	if (FSAL_TEST_MASK(obj_attr->valid_mask, ATTR_ATIME_SERVER)) {
		attr_changed |= XATTR_ATIME | XATTR_ATIME_NOW;
		LogDebug(COMPONENT_FSAL, "new atime = NOW");
	}
	/* Asking to set atime to NOW */
	if (FSAL_TEST_MASK(obj_attr->valid_mask, ATTR_MTIME_SERVER)) {
		attr_changed |= XATTR_MTIME | XATTR_MTIME_NOW;
		LogDebug(COMPONENT_FSAL, "new mtime = NOW");
	}

	/* If any stat changed, indicate that */
	if (attr_changed != 0)
		attr_valid |= XATTR_STAT;

	if (use_acl && FSAL_TEST_MASK(obj_attr->valid_mask, ATTR_ACL)) {
		if (!obj_attr->acl) {
			LogCrit(COMPONENT_FSAL, "setattr acl is NULL");
			return fsalstat(ERR_FSAL_FAULT, 0);
		}

		attr_valid |= XATTR_ACL;
		LogDebug(COMPONENT_FSAL, "setattr acl = %p", obj_attr->acl);

		/* Convert FSAL ACL to GPFS NFS4 ACL and fill buffer. */
		acl_buflen = offsetof(gpfs_acl_t, ace_v1) +
			obj_attr->acl->naces * sizeof(gpfs_ace_v4_t);
		if (acl_buflen > GPFS_ACL_BUF_SIZE)
			acl_buf = gsh_malloc(acl_buflen);
		else
			acl_buf = (gpfs_acl_t *)buffxstat.buffacl;

		status = fsal_acl_2_gpfs_acl(dir_hdl, obj_attr->acl,
					     &buffxstat, acl_buf, acl_buflen);

		if (FSAL_IS_ERROR(status))
			goto acl_free;
	}

	/* If there is any change in stat or acl or both, send it down to fs. */
	if (attr_valid != 0) {
		status = fsal_set_xstat_by_handle(export_fd, ro_ctx,
						  myself->handle, attr_valid,
						  attr_changed, &buffxstat,
						  acl_buf);

		if (FSAL_IS_ERROR(status))
			goto acl_free;
	}

	status = fsalstat(ERR_FSAL_NO_ERROR, 0);

acl_free:
	if (acl_buflen > GPFS_ACL_BUF_SIZE)
		gsh_free(acl_buf);
	return status;
}
