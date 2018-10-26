/*
 * Copyright © 2012, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
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
 * @file   FSAL_CEPH/handle.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date   Mon Jul  9 15:18:47 2012
 *
 * @brief Interface to handle functionality
 *
 * This function implements the interfaces on the struct
 * fsal_obj_handle type.
 */

#include "config.h"
#ifdef LINUX
#include <sys/sysmacros.h> /* for makedev(3) */
#endif
#include <fcntl.h>
#include <cephfs/libcephfs.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_convert.h"
#include "fsal_api.h"
#include "internal.h"
#include "nfs_exports.h"
#include "sal_data.h"
#include "statx_compat.h"
#include "linux/falloc.h"

/**
 * @brief Release an object
 *
 * This function destroys the object referred to by the given handle
 *
 * @param[in] obj_pub The object to release
 *
 * @return FSAL status codes.
 */

static void ceph_fsal_release(struct fsal_obj_handle *obj_pub)
{
	/* The private 'full' handle */
	struct ceph_handle *obj =
			container_of(obj_pub, struct ceph_handle, handle);

	if (obj != obj->export->root)
		deconstruct_handle(obj);
}

/**
 * @brief Look up an object by name
 *
 * This function looks up an object by name in a directory.
 *
 * @param[in]  dir_pub The directory in which to look up the object.
 * @param[in]  path    The name to look up.
 * @param[out] obj_pub The looked up object.
 *
 * @return FSAL status codes.
 */
static fsal_status_t ceph_fsal_lookup(struct fsal_obj_handle *dir_pub,
				      const char *path,
				      struct fsal_obj_handle **obj_pub,
				      struct attrlist *attrs_out)
{
	/* Generic status return */
	int rc = 0;
	/* Stat output */
	struct ceph_statx stx;
	/* The private 'full' export */
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	struct ceph_handle *dir =
		container_of(dir_pub, struct ceph_handle, handle);
	struct ceph_handle *obj = NULL;
	struct Inode *i = NULL;

	LogFullDebug(COMPONENT_FSAL, "Lookup %s", path);

	rc = fsal_ceph_ll_lookup(export->cmount, dir->i, path, &i, &stx,
					!!attrs_out, op_ctx->creds);
	if (rc < 0)
		return ceph2fsal_error(rc);

	construct_handle(&stx, i, export, &obj);

	if (attrs_out != NULL)
		ceph2fsal_attributes(&stx, attrs_out);

	*obj_pub = &obj->handle;

	return fsalstat(0, 0);
}

static int
ceph_fsal_get_sec_label(struct ceph_handle *handle, struct attrlist *attrs)
{
	int rc = 0;
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);

	if (FSAL_TEST_MASK(attrs->request_mask, ATTR4_SEC_LABEL) &&
	    op_ctx_export_has_option(EXPORT_OPTION_SECLABEL_SET)) {
		char label[NFS4_OPAQUE_LIMIT];
		struct user_cred root_creds = {};

		/*
		 * It's possible that the user won't have permission to fetch
		 * the xattrs, so use root creds to get them since it's
		 * supposed to be part of the inode metadata.
		 */
		rc = fsal_ceph_ll_getxattr(export->cmount, handle->i,
					   export->sec_label_xattr, label,
					   NFS4_OPAQUE_LIMIT, &root_creds);
		if (rc < 0) {
			/* If there's no label then just do zero-length one */
			if (rc != -ENODATA)
				goto out_err;
			rc = 0;
		}

		attrs->sec_label.slai_data.slai_data_len = rc;
		gsh_free(attrs->sec_label.slai_data.slai_data_val);
		if (rc > 0) {
			attrs->sec_label.slai_data.slai_data_val =
				gsh_memdup(label, rc);
			FSAL_SET_MASK(attrs->valid_mask, ATTR4_SEC_LABEL);
		} else {
			attrs->sec_label.slai_data.slai_data_val = NULL;
			FSAL_UNSET_MASK(attrs->valid_mask, ATTR4_SEC_LABEL);
		}
	}
out_err:
	return rc;
}

/**
 * @brief Read a directory
 *
 * This function reads the contents of a directory (excluding . and
 * .., which is ironic since the Ceph readdir call synthesizes them
 * out of nothing) and passes dirent information to the supplied
 * callback.
 *
 * @param[in]  dir_pub     The directory to read
 * @param[in]  whence      The cookie indicating resumption, NULL to start
 * @param[in]  dir_state   Opaque, passed to cb
 * @param[in]  cb          Callback that receives directory entries
 * @param[out] eof         True if there are no more entries
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_readdir(struct fsal_obj_handle *dir_pub,
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, attrmask_t attrmask,
				  bool *eof)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	/* The private 'full' directory handle */
	struct ceph_handle *dir =
		container_of(dir_pub, struct ceph_handle, handle);
	/* The director descriptor */
	struct ceph_dir_result *dir_desc = NULL;
	/* Cookie marking the start of the readdir */
	uint64_t start = 0;
	/* ceph_statx want mask */
	unsigned int want = attrmask2ceph_want(attrmask);
	/* Return status */
	fsal_status_t fsal_status = { ERR_FSAL_NO_ERROR, 0 };

	rc = fsal_ceph_ll_opendir(export->cmount, dir->i, &dir_desc,
				  op_ctx->creds);
	if (rc < 0)
		return ceph2fsal_error(rc);

	if (whence != NULL)
		start = *whence;

	ceph_seekdir(export->cmount, dir_desc, start);

	while (!(*eof)) {
		struct ceph_statx stx;
		struct dirent de;
		struct Inode *i = NULL;

		rc = fsal_ceph_readdirplus(export->cmount, dir_desc, dir->i,
					   &de, &stx, want, 0, &i,
					   op_ctx->creds);
		if (rc < 0) {
			fsal_status = ceph2fsal_error(rc);
			goto closedir;
		} else if (rc == 1) {
			struct ceph_handle *obj;
			struct attrlist attrs;
			enum fsal_dir_result cb_rc;

			/* skip . and .. */
			if ((strcmp(de.d_name, ".") == 0)
			    || (strcmp(de.d_name, "..") == 0)) {
				continue;
			}

			construct_handle(&stx, i, export, &obj);

			fsal_prepare_attrs(&attrs, attrmask);
			ceph2fsal_attributes(&stx, &attrs);

			rc = ceph_fsal_get_sec_label(obj, &attrs);
			if (rc < 0) {
				fsal_status = ceph2fsal_error(rc);
				goto closedir;
			}

			cb_rc = cb(de.d_name, &obj->handle, &attrs, dir_state,
					de.d_off);

			fsal_release_attrs(&attrs);

			/* Read ahead not supported by this FSAL. */
			if (cb_rc >= DIR_READAHEAD)
				goto closedir;

		} else if (rc == 0) {
			*eof = true;
		} else {
			/* Can't happen */
			abort();
		}
	}

 closedir:

	rc = ceph_ll_releasedir(export->cmount, dir_desc);

	if (rc < 0)
		fsal_status = ceph2fsal_error(rc);

	return fsal_status;
}

/**
 * @brief Create a directory
 *
 * This function creates a new directory.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * @param[in]     dir_hdl Directory in which to create the directory
 * @param[in]     name    Name of directory to create
 * @param[in]     attrib  Attributes to set on newly created object
 * @param[out]    new_obj Newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_mkdir(struct fsal_obj_handle *dir_hdl,
				const char *name, struct attrlist *attrib,
				struct fsal_obj_handle **new_obj,
				struct attrlist *attrs_out)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	/* The private 'full' directory handle */
	struct ceph_handle *dir =
		container_of(dir_hdl, struct ceph_handle, handle);
	/* Stat result */
	struct ceph_statx stx;
	mode_t unix_mode;
	/* Newly created object */
	struct ceph_handle *obj = NULL;
	struct Inode *i = NULL;
	fsal_status_t status;

	LogFullDebug(COMPONENT_FSAL,
		     "mode = %o uid=%d gid=%d",
		     attrib->mode, (int) op_ctx->creds->caller_uid,
		     (int) op_ctx->creds->caller_gid);

	unix_mode = fsal2unix_mode(attrib->mode)
		& ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	rc = fsal_ceph_ll_mkdir(export->cmount, dir->i, name, unix_mode, &i,
			&stx, !!attrs_out, op_ctx->creds);
	if (rc < 0)
		return ceph2fsal_error(rc);

	construct_handle(&stx, i, export, &obj);

	*new_obj = &obj->handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attrib->valid_mask, ATTR_MODE);

	if (attrib->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*new_obj)->obj_ops->setattr2(*new_obj, false, NULL,
						      attrib);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*new_obj)->obj_ops->release(*new_obj);
			*new_obj = NULL;
		} else if (attrs_out != NULL) {
			/*
			 * We ignore errors here. The mkdir and setattr
			 * succeeded, so we don't want to return error if the
			 * getattrs fails. We'll just return no attributes
			 * in that case.
			 */
			(*new_obj)->obj_ops->getattrs(*new_obj, attrs_out);
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);

		if (attrs_out != NULL) {
			/* Since we haven't set any attributes other than what
			 * was set on create, just use the stat results we used
			 * to create the fsal_obj_handle.
			 */
			ceph2fsal_attributes(&stx, attrs_out);
		}
	}

	FSAL_SET_MASK(attrib->valid_mask, ATTR_MODE);

	return status;
}

/**
 * @brief Create a special file
 *
 * This function creates a new special file.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * @param[in]     dir_hdl  Directory in which to create the object
 * @param[in]     name     Name of object to create
 * @param[in]     nodetype Type of special file to create
 * @param[in]     dev      Major and minor device numbers for block or
 *                         character special
 * @param[in]     attrib   Attributes to set on newly created object
 * @param[out]    new_obj  Newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */
static fsal_status_t ceph_fsal_mknode(struct fsal_obj_handle *dir_hdl,
				      const char *name,
				      object_file_type_t nodetype,
				      struct attrlist *attrib,
				      struct fsal_obj_handle **new_obj,
				      struct attrlist *attrs_out)
{
#ifdef USE_FSAL_CEPH_MKNOD
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	/* The private 'full' directory handle */
	struct ceph_handle *dir =
			container_of(dir_hdl, struct ceph_handle, handle);
	/* Newly opened file descriptor */
	struct Inode *i = NULL;
	/* Status after create */
	struct ceph_statx stx;
	mode_t unix_mode;
	dev_t unix_dev = 0;
	/* Newly created object */
	struct ceph_handle *obj;
	fsal_status_t status;

	unix_mode = fsal2unix_mode(attrib->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	switch (nodetype) {
	case BLOCK_FILE:
		unix_mode |= S_IFBLK;
		unix_dev = makedev(attrib->rawdev.major, attrib->rawdev.minor);
		break;
	case CHARACTER_FILE:
		unix_mode |= S_IFCHR;
		unix_dev = makedev(attrib->rawdev.major, attrib->rawdev.minor);
		break;
	case FIFO_FILE:
		unix_mode |= S_IFIFO;
		break;
	case SOCKET_FILE:
		unix_mode |= S_IFSOCK;
		break;
	default:
		LogMajor(COMPONENT_FSAL, "Invalid node type in FSAL_mknode: %d",
			 nodetype);
		return fsalstat(ERR_FSAL_INVAL, EINVAL);
	}

	rc = fsal_ceph_ll_mknod(export->cmount, dir->i, name, unix_mode,
			unix_dev, &i, &stx, !!attrs_out, op_ctx->creds);
	if (rc < 0)
		return ceph2fsal_error(rc);

	construct_handle(&stx, i, export, &obj);

	*new_obj = &obj->handle;

	/* We handled the mode and rawdev above. */
	FSAL_UNSET_MASK(attrib->valid_mask, ATTR_MODE | ATTR_RAWDEV);

	if (attrib->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*new_obj)->obj_ops->setattr2(*new_obj, false, NULL,
						      attrib);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*new_obj)->obj_ops->release(*new_obj);
			*new_obj = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);

		if (attrs_out != NULL) {
			/* Since we haven't set any attributes other than what
			 * was set on create, just use the stat results we used
			 * to create the fsal_obj_handle.
			 */
			ceph2fsal_attributes(&stx, attrs_out);
		}
	}

	FSAL_SET_MASK(attrib->valid_mask, ATTR_MODE);

	return status;
#else
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
#endif
}

/**
 * @brief Create a symbolic link
 *
 * This function creates a new symbolic link.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * @param[in]     dir_hdl   Directory in which to create the object
 * @param[in]     name      Name of object to create
 * @param[in]     link_path Content of symbolic link
 * @param[in]     attrib    Attributes to set on newly created object
 * @param[out]    new_obj   Newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_symlink(struct fsal_obj_handle *dir_hdl,
				  const char *name, const char *link_path,
				  struct attrlist *attrib,
				  struct fsal_obj_handle **new_obj,
				  struct attrlist *attrs_out)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	/* The private 'full' directory handle */
	struct ceph_handle *dir =
		container_of(dir_hdl, struct ceph_handle, handle);
	/* Stat result */
	struct ceph_statx stx;
	struct Inode *i = NULL;
	/* Newly created object */
	struct ceph_handle *obj = NULL;
	fsal_status_t status;

	rc = fsal_ceph_ll_symlink(export->cmount, dir->i, name, link_path,
			      &i, &stx, !!attrs_out, op_ctx->creds);
	if (rc < 0)
		return ceph2fsal_error(rc);

	construct_handle(&stx, i, export, &obj);

	*new_obj = &obj->handle;

	/* We handled the mode above. */
	FSAL_UNSET_MASK(attrib->valid_mask, ATTR_MODE);

	if (attrib->valid_mask) {
		/* Now per support_ex API, if there are any other attributes
		 * set, go ahead and get them set now.
		 */
		status = (*new_obj)->obj_ops->setattr2(*new_obj, false, NULL,
						      attrib);
		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			LogFullDebug(COMPONENT_FSAL,
				     "setattr2 status=%s",
				     fsal_err_txt(status));
			(*new_obj)->obj_ops->release(*new_obj);
			*new_obj = NULL;
		}
	} else {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);

		if (attrs_out != NULL) {
			/* Since we haven't set any attributes other than what
			 * was set on create, just use the stat results we used
			 * to create the fsal_obj_handle.
			 */
			ceph2fsal_attributes(&stx, attrs_out);
		}
	}

	FSAL_SET_MASK(attrib->valid_mask, ATTR_MODE);

	return status;
}

/**
 * @brief Retrieve the content of a symlink
 *
 * This function allocates a buffer, copying the symlink content into
 * it.
 *
 * @param[in]  link_pub    The handle for the link
 * @param[out] content_buf Buffdesc for symbolic link
 * @param[in]  refresh     true if the underlying content should be
 *                         refreshed.
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_readlink(struct fsal_obj_handle *link_pub,
				   struct gsh_buffdesc *content_buf,
				   bool refresh)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	/* The private 'full' directory handle */
	struct ceph_handle *link =
		container_of(link_pub, struct ceph_handle, handle);
	/* Pointer to the Ceph link content */
	char content[PATH_MAX];

	rc = fsal_ceph_ll_readlink(export->cmount, link->i, content,
				   PATH_MAX, op_ctx->creds);
	if (rc < 0)
		return ceph2fsal_error(rc);

	/* XXX in Ceph through 1/2016, ceph_ll_readlink returns the
	 * length of the path copied (truncated to 32 bits) in rc,
	 * and it cannot exceed the passed buffer size */
	content_buf->addr = gsh_strldup(content, MIN(rc, (PATH_MAX-1)),
					&content_buf->len);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Freshen and return attributes
 *
 * This function freshens and returns the attributes of the given
 * file.
 *
 * @param[in]  handle_pub Object to interrogate
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_getattrs(struct fsal_obj_handle *handle_pub,
					struct attrlist *attrs)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	/* The private 'full' directory handle */
	struct ceph_handle *handle =
		container_of(handle_pub, struct ceph_handle, handle);
	/* Stat buffer */
	struct ceph_statx stx;

	rc = fsal_ceph_ll_getattr(export->cmount, handle->i, &stx,
				CEPH_STATX_ATTR_MASK, op_ctx->creds);
	if (rc < 0)
		goto out_err;

	rc = ceph_fsal_get_sec_label(handle, attrs);
	if (rc < 0)
		goto out_err;

	ceph2fsal_attributes(&stx, attrs);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
out_err:
	if (attrs->request_mask & ATTR_RDATTR_ERR) {
		/* Caller asked for error to be visible. */
		attrs->valid_mask = ATTR_RDATTR_ERR;
	}
	return ceph2fsal_error(rc);
}

/**
 * @brief Create a hard link
 *
 * This function creates a link from the supplied file to a new name
 * in a new directory.
 *
 * @param[in] handle_pub  File to link
 * @param[in] destdir_pub Directory in which to create link
 * @param[in] name        Name of link
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_link(struct fsal_obj_handle *handle_pub,
			       struct fsal_obj_handle *destdir_pub,
			       const char *name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	/* The private 'full' object handle */
	struct ceph_handle *handle =
		container_of(handle_pub, struct ceph_handle, handle);
	/* The private 'full' destination directory handle */
	struct ceph_handle *destdir =
		container_of(destdir_pub, struct ceph_handle, handle);

	rc = fsal_ceph_ll_link(export->cmount, handle->i, destdir->i, name,
				op_ctx->creds);
	if (rc < 0)
		return ceph2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Rename a file
 *
 * This function renames a file, possibly moving it into another
 * directory.  We assume most checks are done by the caller.
 *
 * @param[in] olddir_pub Source directory
 * @param[in] old_name   Original name
 * @param[in] newdir_pub Destination directory
 * @param[in] new_name   New name
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_rename(struct fsal_obj_handle *obj_hdl,
				 struct fsal_obj_handle *olddir_pub,
				 const char *old_name,
				 struct fsal_obj_handle *newdir_pub,
				 const char *new_name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	/* The private 'full' object handle */
	struct ceph_handle *olddir =
		container_of(olddir_pub, struct ceph_handle, handle);
	/* The private 'full' destination directory handle */
	struct ceph_handle *newdir =
		container_of(newdir_pub, struct ceph_handle, handle);

	rc = fsal_ceph_ll_rename(export->cmount, olddir->i, old_name,
					newdir->i, new_name, op_ctx->creds);
	if (rc < 0) {
		/*
		 * RFC5661, section 18.26.3 - renaming on top of a non-empty
		 * directory should return NFS4ERR_EXIST.
		 */
		if (rc == -ENOTEMPTY)
			rc = -EEXIST;
		return ceph2fsal_error(rc);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Remove a name
 *
 * This function removes a name from the filesystem and possibly
 * deletes the associated file.  Directories must be empty to be
 * removed.
 *
 * @param[in] dir_pub Parent directory
 * @param[in] name    Name to remove
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_unlink(struct fsal_obj_handle *dir_pub,
				      struct fsal_obj_handle *obj_pub,
				      const char *name)
{
	/* Generic status return */
	int rc = 0;
	/* The private 'full' export */
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	/* The private 'full' object handle */
	struct ceph_handle *dir =
			container_of(dir_pub, struct ceph_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		     "Unlink %s, I think it's a %s",
		     name, object_file_type_to_str(obj_pub->type));

	if (obj_pub->type != DIRECTORY) {
		rc = fsal_ceph_ll_unlink(export->cmount, dir->i, name,
					op_ctx->creds);
	} else {
		rc = fsal_ceph_ll_rmdir(export->cmount, dir->i, name,
					op_ctx->creds);
	}

	if (rc < 0) {
		LogDebug(COMPONENT_FSAL,
			 "Unlink %s returned %s (%d)",
			 name, strerror(-rc), -rc);
		return ceph2fsal_error(rc);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Open a ceph_fd.
 *
 * @param[in] myself      The ceph internal object handle
 * @param[in] openflags   Mode for open
 * @param[in] posix_flags POSIX open flags for open
 * @param[in] my_fd       The ceph_fd to open
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_open_my_fd(struct ceph_handle *myself,
				     fsal_openflags_t openflags,
				     int posix_flags, struct ceph_fd *my_fd)
{
	int rc;
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);

	LogFullDebug(COMPONENT_FSAL,
		     "my_fd = %p my_fd->fd = %p openflags = %x, posix_flags = %x",
		     my_fd, my_fd->fd, openflags, posix_flags);

	assert(my_fd->fd == NULL
	       && my_fd->openflags == FSAL_O_CLOSED && openflags != 0);

	LogFullDebug(COMPONENT_FSAL,
		     "openflags = %x, posix_flags = %x",
		     openflags, posix_flags);

	rc = fsal_ceph_ll_open(export->cmount, myself->i, posix_flags,
				&my_fd->fd, op_ctx->creds);

	if (rc < 0) {
		my_fd->fd = NULL;
		LogFullDebug(COMPONENT_FSAL,
			     "open failed with %s",
			     strerror(-rc));
		return ceph2fsal_error(rc);
	}

	/* Save the file descriptor, make sure we only save the
	 * open modes that actually represent the open file.
	 */
	LogFullDebug(COMPONENT_FSAL,
		     "fd = %p, new openflags = %x",
		     my_fd->fd, openflags);

	my_fd->openflags = openflags;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t ceph_close_my_fd(struct ceph_handle *handle,
				      struct ceph_fd *my_fd)
{
	int rc = 0;
	fsal_status_t status = fsalstat(ERR_FSAL_NO_ERROR, 0);

	if (my_fd->fd != NULL && my_fd->openflags != FSAL_O_CLOSED) {
		rc = ceph_ll_close(handle->export->cmount, my_fd->fd);
		if (rc < 0)
			status = ceph2fsal_error(rc);
		my_fd->fd = NULL;
		my_fd->openflags = FSAL_O_CLOSED;
	}

	return status;
}

/**
 * @brief Function to open an fsal_obj_handle's global file descriptor.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  openflags   Mode for open
 * @param[out] fd          File descriptor that is to be used
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_open_func(struct fsal_obj_handle *obj_hdl,
				    fsal_openflags_t openflags,
				    struct fsal_fd *fd)
{
	int posix_flags = 0;

	fsal2posix_openflags(openflags, &posix_flags);

	return ceph_open_my_fd(container_of(obj_hdl,
		struct ceph_handle, handle), openflags,
		posix_flags, (struct ceph_fd *)fd);
}

/**
 * @brief Function to close an fsal_obj_handle's global file descriptor.
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  fd          File handle to close
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_close_func(struct fsal_obj_handle *obj_hdl,
				     struct fsal_fd *fd)
{
	return ceph_close_my_fd(container_of(obj_hdl,
		struct ceph_handle, handle), (struct ceph_fd *)fd);
}

/**
 * @brief Close a file
 *
 * This function closes a file, freeing resources used for read/write
 * access and releasing capabilities.
 *
 * @param[in] obj_hdl File to close
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_close(struct fsal_obj_handle *obj_hdl)
{
	fsal_status_t status;
	/* The private 'full' object handle */
	struct ceph_handle *handle =
			container_of(obj_hdl, struct ceph_handle, handle);

	if (handle->fd.openflags == FSAL_O_CLOSED)
		return fsalstat(ERR_FSAL_NOT_OPENED, 0);

	/* Take write lock on object to protect file descriptor.
	 * This can block over an I/O operation.
	 */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	status = ceph_close_my_fd(handle, &handle->fd);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}

/**
 * @brief Allocate a state_t structure
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param[in] exp_hdl               Export state_t will be associated with
 * @param[in] state_type            Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns a state structure.
 */

struct state_t *ceph_alloc_state(struct fsal_export *exp_hdl,
				 enum state_type state_type,
				 struct state_t *related_state)
{
	struct state_t *state;
	struct ceph_fd *my_fd;

	state = init_state(gsh_calloc(1, sizeof(struct ceph_state_fd)),
			   exp_hdl, state_type, related_state);

	my_fd = &container_of(state, struct ceph_state_fd, state)->ceph_fd;

	my_fd->fd = NULL;
	my_fd->openflags = FSAL_O_CLOSED;
	PTHREAD_RWLOCK_init(&my_fd->fdlock, NULL);

	return state;
}

/**
 * @brief free a ceph_state_fd structure
 *
 * @param[in] exp_hdl  Export state_t will be associated with
 * @param[in] state    Related state if appropriate
 *
 */
void ceph_free_state(struct fsal_export *exp_hdl, struct state_t *state)
{
	struct ceph_state_fd *state_fd = container_of(state,
						      struct ceph_state_fd,
						      state);
	struct ceph_fd *my_fd = &state_fd->ceph_fd;

	PTHREAD_RWLOCK_destroy(&my_fd->fdlock);

	gsh_free(state_fd);
}

/**
 * @brief Merge a duplicate handle with an original handle
 *
 * This function is used if an upper layer detects that a duplicate
 * object handle has been created. It allows the FSAL to merge anything
 * from the duplicate back into the original.
 *
 * The caller must release the object (the caller may have to close
 * files if the merge is unsuccessful).
 *
 * @param[in]  orig_hdl  Original handle
 * @param[in]  dupe_hdl Handle to merge into original
 *
 * @return FSAL status.
 *
 */

static fsal_status_t ceph_fsal_merge(struct fsal_obj_handle *orig_hdl,
				     struct fsal_obj_handle *dupe_hdl)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	if (orig_hdl->type == REGULAR_FILE &&
	    dupe_hdl->type == REGULAR_FILE) {
		/* We need to merge the share reservations on this file.
		 * This could result in ERR_FSAL_SHARE_DENIED.
		 */
		struct ceph_handle *orig, *dupe;

		orig = container_of(orig_hdl, struct ceph_handle, handle);
		dupe = container_of(dupe_hdl, struct ceph_handle, handle);

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&orig_hdl->obj_lock);

		status = merge_share(&orig->share, &dupe->share);

		PTHREAD_RWLOCK_unlock(&orig_hdl->obj_lock);
	}

	return status;
}

static bool
ceph_check_verifier_stat(struct ceph_statx *stx, fsal_verifier_t verifier)
{
	uint32_t verf_hi, verf_lo;

	memcpy(&verf_hi, verifier, sizeof(uint32_t));
	memcpy(&verf_lo, verifier + sizeof(uint32_t), sizeof(uint32_t));

	LogFullDebug(COMPONENT_FSAL,
		     "Passed verifier %"PRIx32" %"PRIx32
		     " file verifier %"PRIx32" %"PRIx32,
		     verf_hi, verf_lo,
		     (uint32_t)stx->stx_atime.tv_sec,
		     (uint32_t)stx->stx_mtime.tv_sec);

	return stx->stx_atime.tv_sec == verf_hi &&
	       stx->stx_mtime.tv_sec == verf_lo;
}

/**
 * @brief Open a file descriptor for read or write and possibly create
 *
 * This function opens a file for read or write, possibly creating it.
 * If the caller is passing a state, it must hold the state_lock
 * exclusive.
 *
 * state can be NULL which indicates a stateless open (such as via the
 * NFS v3 CREATE operation), in which case the FSAL must assure protection
 * of any resources. If the file is being created, such protection is
 * simple since no one else will have access to the object yet, however,
 * in the case of an exclusive create, the common resources may still need
 * protection.
 *
 * If Name is NULL, obj_hdl is the file itself, otherwise obj_hdl is the
 * parent directory.
 *
 * On an exclusive create, the upper layer may know the object handle
 * already, so it MAY call with name == NULL. In this case, the caller
 * expects just to check the verifier.
 *
 * On a call with an existing object handle for an UNCHECKED create,
 * we can set the size to 0.
 *
 * If attributes are not set on create, the FSAL will set some minimal
 * attributes (for example, mode might be set to 0600).
 *
 * If an open by name succeeds and did not result in Ganesha creating a file,
 * the caller will need to do a subsequent permission check to confirm the
 * open. This is because the permission attributes were not available
 * beforehand.
 *
 * @param[in] obj_hdl               File to open or parent directory
 * @param[in,out] state             state_t to use for this operation
 * @param[in] openflags             Mode for open
 * @param[in] createmode            Mode for create
 * @param[in] name                  Name for file if being created or opened
 * @param[in] attrib_set            Attributes to set on created file
 * @param[in] verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj           Newly created object
 * @param[in,out] caller_perm_check The caller must do a permission check
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_open2(struct fsal_obj_handle *obj_hdl,
				 struct state_t *state,
				 fsal_openflags_t openflags,
				 enum fsal_create_mode createmode,
				 const char *name,
				 struct attrlist *attrib_set,
				 fsal_verifier_t verifier,
				 struct fsal_obj_handle **new_obj,
				 struct attrlist *attrs_out,
				 bool *caller_perm_check)
{
	int posix_flags = 0;
	int retval = 0;
	mode_t unix_mode = 0;
	fsal_status_t status = {0, 0};
	struct ceph_fd *my_fd = NULL;
	struct ceph_handle *myself, *hdl = NULL;
	struct ceph_statx stx;
	bool truncated;
	bool created = false;
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	struct Inode *i = NULL;
	Fh *fd;

	LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
		    "attrs ", attrib_set, false);

	if (state != NULL)
		my_fd = &container_of(state, struct ceph_state_fd,
				      state)->ceph_fd;

	myself = container_of(obj_hdl, struct ceph_handle, handle);

	fsal2posix_openflags(openflags, &posix_flags);

	truncated = (posix_flags & O_TRUNC) != 0;

	if (createmode >= FSAL_EXCLUSIVE) {
		/* Now fixup attrs for verifier if exclusive create */
		set_common_verifier(attrib_set, verifier);
	}

	if (name == NULL) {
		/* This is an open by handle */
		if (state != NULL) {
			/* Prepare to take the share reservation, but only if we
			 * are called with a valid state (if state is NULL the
			 * caller is a stateless create such as NFS v3 CREATE).
			 */

			/* This can block over an I/O operation. */
			PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

			/* Check share reservation conflicts. */
			status = check_share_conflict(&myself->share,
						      openflags, false);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
				return status;
			}

			/* Take the share reservation now by updating the
			 * counters.
			 */
			update_share_counters(&myself->share, FSAL_O_CLOSED,
					      openflags);

			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
		} else {
			/* We need to use the global fd to continue, and take
			 * the lock to protect it.
			 */
			my_fd = &myself->fd;
			PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);
		}

		if (my_fd->openflags != FSAL_O_CLOSED) {
			ceph_close_my_fd(myself, my_fd);
		}
		status = ceph_open_my_fd(myself, openflags, posix_flags, my_fd);

		if (FSAL_IS_ERROR(status)) {
			if (state == NULL) {
				/* Release the lock taken above, and return
				 * since there is nothing to undo.
				 */
				PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
				return status;
			} else {
				/* Error - need to release the share */
				goto undo_share;
			}
		}

		if (createmode >= FSAL_EXCLUSIVE || truncated) {
			/* Refresh the attributes */
			retval = fsal_ceph_ll_getattr(export->cmount,
					myself->i, &stx, !!attrs_out,
					op_ctx->creds);

			if (retval == 0) {
				LogFullDebug(COMPONENT_FSAL,
					     "New size = %"PRIx64,
					     stx.stx_size);
			} else {
				/* Because we have an inode ref, we never
				 * get EBADF like other FSALs might see.
				 */
				status = ceph2fsal_error(retval);
			}

			/* Now check verifier for exclusive, but not for
			 * FSAL_EXCLUSIVE_9P.
			 */
			if (!FSAL_IS_ERROR(status) &&
			    createmode >= FSAL_EXCLUSIVE &&
			    createmode != FSAL_EXCLUSIVE_9P &&
			    !ceph_check_verifier_stat(&stx, verifier)) {
				/* Verifier didn't match, return EEXIST */
				status =
				    fsalstat(posix2fsal_error(EEXIST), EEXIST);
			}

			if (attrs_out) {
				/* Save out new attributes */
				ceph2fsal_attributes(&stx, attrs_out);
			}
		} else if (attrs_out && attrs_out->request_mask &
			   ATTR_RDATTR_ERR) {
			attrs_out->valid_mask = ATTR_RDATTR_ERR;
		}

		if (state == NULL) {
			/* If no state, release the lock taken above and return
			 * status. If success, we haven't done any permission
			 * check so ask the caller to do so.
			 */
			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
			*caller_perm_check = !FSAL_IS_ERROR(status);
			return status;
		}

		if (!FSAL_IS_ERROR(status)) {
			/* Return success. We haven't done any permission
			 * check so ask the caller to do so.
			 */
			*caller_perm_check = true;
			return status;
		}

		(void) ceph_close_my_fd(myself, my_fd);

 undo_share:

		/* Can only get here with state not NULL and an error */

		/* On error we need to release our share reservation
		 * and undo the update of the share counters.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		update_share_counters(&myself->share, openflags, FSAL_O_CLOSED);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

		return status;
	}

	/* In this path where we are opening by name, we can't check share
	 * reservation yet since we don't have an object_handle yet. If we
	 * indeed create the object handle (there is no race with another
	 * open by name), then there CAN NOT be a share conflict, otherwise
	 * the share conflict will be resolved when the object handles are
	 * merged.
	 */

	if (createmode == FSAL_NO_CREATE) {
		/* Non creation case, libcephfs doesn't have open by name so we
		 * have to do a lookup and then handle as an open by handle.
		 */
		struct fsal_obj_handle *temp = NULL;

		/* We don't have open by name... */
		status = obj_hdl->obj_ops->lookup(obj_hdl, name, &temp, NULL);

		if (FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_FSAL,
				     "lookup returned %s",
				     fsal_err_txt(status));
			return status;
		}

		/* Now call ourselves without name and attributes to open. */
		status = obj_hdl->obj_ops->open2(temp, state, openflags,
						FSAL_NO_CREATE, NULL, NULL,
						verifier, new_obj,
						attrs_out,
						caller_perm_check);

		if (FSAL_IS_ERROR(status)) {
			/* Release the object we found by lookup. */
			temp->obj_ops->release(temp);
			LogFullDebug(COMPONENT_FSAL,
				     "open returned %s",
				     fsal_err_txt(status));
		}

		return status;
	}

	/* Now add in O_CREAT and O_EXCL.
	 * Even with FSAL_UNGUARDED we try exclusive create first so
	 * we can safely set attributes.
	 */
	if (createmode != FSAL_NO_CREATE) {
		/* Now add in O_CREAT and O_EXCL. */
		posix_flags |= O_CREAT;

		/* And if we are at least FSAL_GUARDED, do an O_EXCL create. */
		if (createmode >= FSAL_GUARDED)
			posix_flags |= O_EXCL;

		/* Fetch the mode attribute to use in the openat system call. */
		unix_mode = fsal2unix_mode(attrib_set->mode) &
		    ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

		/* Don't set the mode if we later set the attributes */
		FSAL_UNSET_MASK(attrib_set->valid_mask, ATTR_MODE);
	}

	if (createmode == FSAL_UNCHECKED && (attrib_set->valid_mask != 0)) {
		/* If we have FSAL_UNCHECKED and want to set more attributes
		 * than the mode, we attempt an O_EXCL create first, if that
		 * succeeds, then we will be allowed to set the additional
		 * attributes, otherwise, we don't know we created the file
		 * and this can NOT set the attributes.
		 */
		posix_flags |= O_EXCL;
	}

	retval = fsal_ceph_ll_create(export->cmount,  myself->i, name,
				unix_mode, posix_flags, &i, &fd, &stx,
				!!attrs_out, op_ctx->creds);

	if (retval < 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "Create %s failed with %s",
			     name, strerror(-retval));
	}

	if (retval == -EEXIST && createmode == FSAL_UNCHECKED) {
		/* We tried to create O_EXCL to set attributes and failed.
		 * Remove O_EXCL and retry, also remember not to set attributes.
		 * We still try O_CREAT again just in case file disappears out
		 * from under us.
		 *
		 * Note that because we have dropped O_EXCL, later on we will
		 * not assume we created the file, and thus will not set
		 * additional attributes. We don't need to separately track
		 * the condition of not wanting to set attributes.
		 */
		posix_flags &= ~O_EXCL;
		retval = fsal_ceph_ll_create(export->cmount,  myself->i,
				name, unix_mode, posix_flags, &i, &fd,
				&stx, !!attrs_out, op_ctx->creds);
		if (retval < 0) {
			LogFullDebug(COMPONENT_FSAL,
				     "Non-exclusive Create %s failed with %s",
				     name, strerror(-retval));
		}
	}

	if (retval < 0) {
		return ceph2fsal_error(retval);
	}

	/* Remember if we were responsible for creating the file.
	 * Note that in an UNCHECKED retry we MIGHT have re-created the
	 * file and won't remember that. Oh well, so in that rare case we
	 * leak a partially created file if we have a subsequent error in here.
	 */
	created = (posix_flags & O_EXCL) != 0;

	/** @todo FSF: Note that the current implementation of ceph_ll_create
	 *             does not accept an alt groups list, so it is possible
	 *             a create (including an UNCHECKED create on an already
	 *             existing file) would fail because the directory or
	 *             file was owned by a group other than the primary group.
	 *             Conversely, it could also succeed when it should have
	 *             failed if other is granted more permission than
	 *             one of the alt groups).
	 */

	/* Since we did the ceph_ll_create using the caller's credentials,
	 * we don't need to do an additional permission check.
	 */
	*caller_perm_check = false;

	construct_handle(&stx, i, export, &hdl);

	/* If we didn't have a state above, use the global fd. At this point,
	 * since we just created the global fd, no one else can have a
	 * reference to it, and thus we can mamnipulate unlocked which is
	 * handy since we can then call setattr2 which WILL take the lock
	 * without a double locking deadlock.
	 */
	if (my_fd == NULL)
		my_fd = &hdl->fd;

	my_fd->fd = fd;
	my_fd->openflags = openflags;

	*new_obj = &hdl->handle;

	if (created && attrib_set->valid_mask != 0) {
		/* Set attributes using our newly opened file descriptor as the
		 * share_fd if there are any left to set (mode and truncate
		 * have already been handled).
		 *
		 * Note that we only set the attributes if we were responsible
		 * for creating the file and we have attributes to set.
		 */
		status = (*new_obj)->obj_ops->setattr2(*new_obj,
						      false,
						      state,
						      attrib_set);

		if (FSAL_IS_ERROR(status))
			goto fileerr;

		if (attrs_out != NULL) {
			status = (*new_obj)->obj_ops->getattrs(*new_obj,
							      attrs_out);
			if (FSAL_IS_ERROR(status) &&
			    (attrs_out->request_mask & ATTR_RDATTR_ERR) == 0) {
				/* Get attributes failed and caller expected
				 * to get the attributes. Otherwise continue
				 * with attrs_out indicating ATTR_RDATTR_ERR.
				 */
				goto fileerr;
			}
		}
	} else if (attrs_out != NULL) {
		/* Since we haven't set any attributes other than what was set
		 * on create (if we even created), just use the stat results
		 * we used to create the fsal_obj_handle.
		 */
		ceph2fsal_attributes(&stx, attrs_out);
	}

	if (state != NULL) {
		/* Prepare to take the share reservation, but only if we are
		 * called with a valid state (if state is NULL the caller is
		 * a stateless create such as NFS v3 CREATE).
		 */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&(*new_obj)->obj_lock);

		/* Take the share reservation now by updating the counters. */
		update_share_counters(&hdl->share, FSAL_O_CLOSED, openflags);

		PTHREAD_RWLOCK_unlock(&(*new_obj)->obj_lock);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:

	/* Close the file we just opened. */
	(void) ceph_close_my_fd(container_of(*new_obj,
		struct ceph_handle, handle), my_fd);

	/* Release the handle we just allocated. */
	(*new_obj)->obj_ops->release(*new_obj);
	*new_obj = NULL;

	if (created) {
		/* Remove the file we just created */
		fsal_ceph_ll_unlink(export->cmount, myself->i, name,
					op_ctx->creds);
	}

	return status;
}

/**
 * @brief Return open status of a state.
 *
 * This function returns open flags representing the current open
 * status for a state. The state_lock must be held.
 *
 * @param[in] obj_hdl     File on which to operate
 * @param[in] state       File state to interrogate
 *
 * @retval Flags representing current open status
 */

static fsal_openflags_t ceph_fsal_status2(struct fsal_obj_handle *obj_hdl,
					  struct state_t *state)
{
	struct ceph_fd *my_fd = (struct ceph_fd *)(state + 1);

	return my_fd->openflags;
}

/**
 * @brief Re-open a file that may be already opened
 *
 * This function supports changing the access mode of a share reservation and
 * thus should only be called with a share state. The state_lock must be held.
 *
 * This MAY be used to open a file the first time if there is no need for
 * open by name or create semantics. One example would be 9P lopen.
 *
 * @param[in] obj_hdl     File on which to operate
 * @param[in] state       state_t to use for this operation
 * @param[in] openflags   Mode for re-open
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_reopen2(struct fsal_obj_handle *obj_hdl,
				       struct state_t *state,
				       fsal_openflags_t openflags)
{
	struct ceph_fd temp_fd = {
			FSAL_O_CLOSED, PTHREAD_RWLOCK_INITIALIZER, NULL };
	struct ceph_fd *my_fd = &temp_fd, *my_share_fd;
	struct ceph_handle *myself =
			container_of(obj_hdl, struct ceph_handle, handle);
	fsal_status_t status = {0, 0};
	int posix_flags = 0;
	fsal_openflags_t old_openflags;

	my_share_fd = &container_of(state, struct ceph_state_fd,
				    state)->ceph_fd;

	fsal2posix_openflags(openflags, &posix_flags);

	/* This can block over an I/O operation. */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	old_openflags = my_share_fd->openflags;

	/* We can conflict with old share, so go ahead and check now. */
	status = check_share_conflict(&myself->share, openflags, false);

	if (FSAL_IS_ERROR(status)) {
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

		return status;
	}

	/* Set up the new share so we can drop the lock and not have a
	 * conflicting share be asserted, updating the share counters.
	 */
	update_share_counters(&myself->share, old_openflags, openflags);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	status = ceph_open_my_fd(myself, openflags, posix_flags, my_fd);

	if (!FSAL_IS_ERROR(status)) {
		/* Close the existing file descriptor and copy the new
		 * one over. Make sure no one is using the fd that we are
		 * about to close!
		 */
		PTHREAD_RWLOCK_wrlock(&my_share_fd->fdlock);

		ceph_close_my_fd(myself, my_share_fd);
		my_share_fd->fd = my_fd->fd;
		my_share_fd->openflags = my_fd->openflags;

		PTHREAD_RWLOCK_unlock(&my_share_fd->fdlock);
	} else {
		/* We had a failure on open - we need to revert the share.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		update_share_counters(&myself->share, openflags, old_openflags);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
	}

	return status;
}

/**
 * @brief Find a file descriptor for a read or write operation.
 *
 * We do not need file descriptors for non-regular files, so this never has to
 * handle them.
 */
static fsal_status_t ceph_find_fd(Fh **fd,
			   struct fsal_obj_handle *obj_hdl,
			   bool bypass,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   bool *has_lock,
			   bool *closefd,
			   bool open_for_locks)
{
	struct ceph_handle *myself =
			container_of(obj_hdl, struct ceph_handle, handle);
	struct ceph_fd temp_fd = {
			FSAL_O_CLOSED, PTHREAD_RWLOCK_INITIALIZER, NULL };
	struct ceph_fd *out_fd = &temp_fd;
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	bool reusing_open_state_fd = false;

	status = fsal_find_fd((struct fsal_fd **)&out_fd, obj_hdl,
			      (struct fsal_fd *)&myself->fd, &myself->share,
			      bypass, state, openflags,
			      ceph_open_func, ceph_close_func,
			      has_lock, closefd, open_for_locks,
			      &reusing_open_state_fd);

	LogFullDebug(COMPONENT_FSAL,
		     "fd = %p", out_fd->fd);
	*fd = out_fd->fd;
	return status;
}

/**
 * @brief Read data from a file
 *
 * This function reads data from the given file. The FSAL must be able to
 * perform the read whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations.  This is
 * an (optionally) asynchronous call.  When the I/O is complete, the done
 * callback is called with the results.
 *
 * @param[in]     obj_hdl	File on which to operate
 * @param[in]     bypass	If state doesn't indicate a share reservation,
 *				bypass any deny read
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] read_arg	Info about read, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 *
 * @return Nothing; results are in callback
 */

static void ceph_fsal_read2(struct fsal_obj_handle *obj_hdl, bool bypass,
			    fsal_async_cb done_cb, struct fsal_io_arg *read_arg,
			    void *caller_arg)
{
	struct ceph_handle *myself =
			container_of(obj_hdl, struct ceph_handle, handle);
	Fh *my_fd = NULL;
	ssize_t nb_read;
	fsal_status_t status;
	bool has_lock = false;
	bool closefd = false;
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	struct ceph_fd *ceph_fd = NULL;
	uint64_t offset = read_arg->offset;
	int i;

	if (read_arg->info != NULL) {
		/* Currently we don't support READ_PLUS */
		done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0), read_arg,
			caller_arg);
		return;
	}

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (read_arg->state) {
		ceph_fd = &container_of(read_arg->state, struct ceph_state_fd,
					state)->ceph_fd;

		PTHREAD_RWLOCK_rdlock(&ceph_fd->fdlock);
	}

	/* Get a usable file descriptor */
	status = ceph_find_fd(&my_fd, obj_hdl, bypass, read_arg->state,
			      FSAL_O_READ, &has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status))
		goto out;

	read_arg->io_amount = 0;

	for (i = 0; i < read_arg->iov_count; i++) {
		nb_read = ceph_ll_read(export->cmount, my_fd, offset,
				       read_arg->iov[i].iov_len,
				       read_arg->iov[i].iov_base);

		if (nb_read == 0) {
			read_arg->end_of_file = true;
			break;
		} else if (nb_read < 0) {
			status = ceph2fsal_error(nb_read);
			goto out;
		}

		read_arg->io_amount += nb_read;
		offset += nb_read;
	}

#if 0
	/** @todo
	 *
	 * Is this all we really need to do to support READ_PLUS? Will anyone
	 * ever get upset that we don't return holes, even for blocks of all
	 * zeroes?
	 *
	 */
	if (info != NULL) {
		info->io_content.what = NFS4_CONTENT_DATA;
		info->io_content.data.d_offset = offset + nb_read;
		info->io_content.data.d_data.data_len = nb_read;
		info->io_content.data.d_data.data_val = buffer;
	}
#endif

 out:

	if (ceph_fd)
		PTHREAD_RWLOCK_unlock(&ceph_fd->fdlock);

	if (closefd)
		(void) ceph_ll_close(myself->export->cmount, my_fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	done_cb(obj_hdl, status, read_arg, caller_arg);
}

/**
 * @brief Write data to a file
 *
 * This function writes data to a file. The FSAL must be able to
 * perform the write whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations. Even
 * with bypass == true, it will enforce a mandatory (NFSv4) deny_write if
 * an appropriate state is not passed).
 *
 * The FSAL is expected to enforce sync if necessary.
 *
 * @param[in]     obj_hdl        File on which to operate
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any non-mandatory deny write
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] write_arg	Info about write, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 */

static void ceph_fsal_write2(struct fsal_obj_handle *obj_hdl, bool bypass,
			     fsal_async_cb done_cb,
			     struct fsal_io_arg *write_arg, void *caller_arg)
{
	struct ceph_handle *myself =
			container_of(obj_hdl, struct ceph_handle, handle);
	ssize_t nb_written;
	fsal_status_t status;
	int i, retval = 0;
	Fh *my_fd = NULL;
	bool has_lock = false;
	bool closefd = false;
	fsal_openflags_t openflags = FSAL_O_WRITE;
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	struct ceph_fd *ceph_fd = NULL;
	uint64_t offset = write_arg->offset;

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (write_arg->state) {
		ceph_fd = &container_of(write_arg->state, struct ceph_state_fd,
					state)->ceph_fd;

		PTHREAD_RWLOCK_rdlock(&ceph_fd->fdlock);
	}

	/* Get a usable file descriptor */
	status = ceph_find_fd(&my_fd, obj_hdl, bypass, write_arg->state,
			      openflags, &has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "find_fd failed %s", msg_fsal_err(status.major));
		goto out;
	}

	for (i = 0; i < write_arg->iov_count; i++) {
		nb_written =
			ceph_ll_write(export->cmount, my_fd, offset,
				      write_arg->iov[i].iov_len,
				      write_arg->iov[i].iov_base);

		if (nb_written == 0) {
			break;
		} else if (nb_written < 0) {
			status = ceph2fsal_error(nb_written);
			goto out;
		}

		write_arg->io_amount += nb_written;
		offset += nb_written;
	}

	if (write_arg->fsal_stable) {
		retval = ceph_ll_fsync(export->cmount, my_fd, false);

		if (retval < 0) {
			status = ceph2fsal_error(retval);
			write_arg->fsal_stable = false;
		}
	}

 out:

	if (ceph_fd)
		PTHREAD_RWLOCK_unlock(&ceph_fd->fdlock);

	if (closefd)
		(void) ceph_ll_close(myself->export->cmount, my_fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	done_cb(obj_hdl, status, write_arg, caller_arg);
}

/**
 * @brief Commit written data
 *
 * This function flushes possibly buffered data to a file. This method
 * differs from commit due to the need to interact with share reservations
 * and the fact that the FSAL manages the state of "file descriptors". The
 * FSAL must be able to perform this operation without being passed a specific
 * state.
 *
 * @param[in] obj_hdl          File on which to operate
 * @param[in] state            state_t to use for this operation
 * @param[in] offset           Start of range to commit
 * @param[in] len              Length of range to commit
 *
 * @return FSAL status.
 */

#ifdef USE_FSAL_CEPH_LL_SYNC_INODE
static fsal_status_t ceph_fsal_commit2(struct fsal_obj_handle *obj_hdl,
				       off_t offset,
				       size_t len)
{
	int retval;
	struct ceph_handle *myself =
			container_of(obj_hdl, struct ceph_handle, handle);
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);

	/*
	 * If we have the ceph_ll_sync_inode call, then we can avoid opening
	 * altogether.
	 */
	retval = ceph_ll_sync_inode(export->cmount, myself->i, 0);
	return ceph2fsal_error(retval);
}
#else
static fsal_status_t ceph_fsal_commit2(struct fsal_obj_handle *obj_hdl,
				       off_t offset,
				       size_t len)
{
	struct ceph_handle *myself =
			container_of(obj_hdl, struct ceph_handle, handle);
	fsal_status_t status;
	int retval;
	struct ceph_fd temp_fd = {
			FSAL_O_CLOSED, PTHREAD_RWLOCK_INITIALIZER, NULL };
	struct ceph_fd *out_fd = &temp_fd;
	bool has_lock = false;
	bool closefd = false;
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	struct user_cred root_creds = {};
	struct user_cred *saved_creds = op_ctx->creds;


	/*
	 * Make sure file is open in appropriate mode, without checking for
	 * share reservation. Also, it's possible that the file has changed
	 * permissions since it was opened by the writer, so open the file
	 * with root creds here since we're just doing a fsync.
	 */
	op_ctx->creds = &root_creds;
	status = fsal_reopen_obj(obj_hdl, false, false, FSAL_O_WRITE,
				 (struct fsal_fd *)&myself->fd, &myself->share,
				 ceph_open_func, ceph_close_func,
				 (struct fsal_fd **)&out_fd, &has_lock,
				 &closefd);
	op_ctx->creds = saved_creds;

	if (!FSAL_IS_ERROR(status)) {
		retval = ceph_ll_fsync(export->cmount, out_fd->fd, false);

		if (retval < 0)
			status = ceph2fsal_error(retval);
	}

	if (closefd)
		(void) ceph_ll_close(myself->export->cmount, out_fd->fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}
#endif

#ifdef USE_FSAL_CEPH_SETLK
/**
 * @brief Perform a lock operation
 *
 * This function performs a lock operation (lock, unlock, test) on a
 * file. This method assumes the FSAL is able to support lock owners,
 * though it need not support asynchronous blocking locks. Passing the
 * lock state allows the FSAL to associate information with a specific
 * lock owner for each file (which may include use of a "file descriptor".
 *
 * For FSAL_VFS etc. we ignore owner, implicitly we have a lock_fd per
 * lock owner (i.e. per state).
 *
 * @param[in]  obj_hdl          File on which to operate
 * @param[in]  state            state_t to use for this operation
 * @param[in]  owner            Lock owner
 * @param[in]  lock_op          Operation to perform
 * @param[in]  request_lock     Lock to take/release/test
 * @param[out] conflicting_lock Conflicting lock
 *
 * @return FSAL status.
 */
static fsal_status_t ceph_fsal_lock_op2(struct fsal_obj_handle *obj_hdl,
					struct state_t *state, void *owner,
					fsal_lock_op_t lock_op,
					fsal_lock_param_t *request_lock,
					fsal_lock_param_t *conflicting_lock)
{
	struct ceph_handle *myself =
			container_of(obj_hdl, struct ceph_handle, handle);
	struct flock lock_args;
	fsal_status_t status = {0, 0};
	int retval = 0;
	Fh *my_fd = NULL;
	bool has_lock = false;
	bool closefd = false;
	bool bypass = false;
	fsal_openflags_t openflags = FSAL_O_RDWR;
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	struct ceph_fd *ceph_fd = NULL;

	LogFullDebug(COMPONENT_FSAL,
		     "Locking: op:%d type:%d start:%" PRIu64 " length:%"
		     PRIu64 " ",
		     lock_op, request_lock->lock_type, request_lock->lock_start,
		     request_lock->lock_length);

	if (lock_op == FSAL_OP_LOCKT) {
		/* We may end up using global fd, don't fail on a deny mode */
		bypass = true;
		openflags = FSAL_O_ANY;
	} else if (lock_op == FSAL_OP_LOCK) {
		if (request_lock->lock_type == FSAL_LOCK_R)
			openflags = FSAL_O_READ;
		else if (request_lock->lock_type == FSAL_LOCK_W)
			openflags = FSAL_O_WRITE;
	} else if (lock_op == FSAL_OP_UNLOCK) {
		openflags = FSAL_O_ANY;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: Lock operation requested was not TEST, READ, or WRITE.");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	if (lock_op != FSAL_OP_LOCKT && state == NULL) {
		LogCrit(COMPONENT_FSAL, "Non TEST operation with NULL state");
		return fsalstat(posix2fsal_error(EINVAL), EINVAL);
	}

	if (request_lock->lock_type == FSAL_LOCK_R) {
		lock_args.l_type = F_RDLCK;
	} else if (request_lock->lock_type == FSAL_LOCK_W) {
		lock_args.l_type = F_WRLCK;
	} else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: The requested lock type was not read or write.");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	if (lock_op == FSAL_OP_UNLOCK)
		lock_args.l_type = F_UNLCK;

	lock_args.l_pid = 0;
	lock_args.l_len = request_lock->lock_length;
	lock_args.l_start = request_lock->lock_start;
	lock_args.l_whence = SEEK_SET;

	/* flock.l_len being signed long integer, larger lock ranges may
	 * get mapped to negative values. As per 'man 3 fcntl', posix
	 * locks can accept negative l_len values which may lead to
	 * unlocking an unintended range. Better bail out to prevent that.
	 */
	if (lock_args.l_len < 0) {
		LogCrit(COMPONENT_FSAL,
			"The requested lock length is out of range- lock_args.l_len(%ld), request_lock_length(%"
			PRIu64 ")",
			lock_args.l_len, request_lock->lock_length);
		return fsalstat(ERR_FSAL_BAD_RANGE, 0);
	}

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (state) {
		ceph_fd = &container_of(state, struct ceph_state_fd,
					state)->ceph_fd;

		PTHREAD_RWLOCK_rdlock(&ceph_fd->fdlock);
	}

	/* Get a usable file descriptor */
	status = ceph_find_fd(&my_fd, obj_hdl, bypass, state, openflags,
			      &has_lock, &closefd, true);

	if (FSAL_IS_ERROR(status)) {
		LogCrit(COMPONENT_FSAL, "Unable to find fd for lock operation");
		return status;
	}

	if (lock_op == FSAL_OP_LOCKT) {
		retval = ceph_ll_getlk(export->cmount, my_fd, &lock_args,
				       (uint64_t) owner);
	} else {
		retval = ceph_ll_setlk(export->cmount, my_fd, &lock_args,
				       (uint64_t) owner, false);
	}

	if (retval < 0) {
		LogDebug(COMPONENT_FSAL,
			 "%s returned %d %s",
			 lock_op == FSAL_OP_LOCKT
				? "ceph_ll_getlk" : "ceph_ll_setlk",
			 -retval, strerror(-retval));

		if (conflicting_lock != NULL) {
			int retval2;

			/* Get the conflicting lock */
			retval2 = ceph_ll_getlk(export->cmount, my_fd,
					       &lock_args, (uint64_t) owner);

			if (retval2 < 0) {
				LogCrit(COMPONENT_FSAL,
					"After failing a lock request, I couldn't even get the details of who owns the lock, error %d %s",
					-retval2, strerror(-retval2));
				goto err;
			}

			conflicting_lock->lock_length = lock_args.l_len;
			conflicting_lock->lock_start = lock_args.l_start;
			conflicting_lock->lock_type = lock_args.l_type;
		}

		goto err;
	}

	/* F_UNLCK is returned then the tested operation would be possible. */
	if (conflicting_lock != NULL) {
		if (lock_op == FSAL_OP_LOCKT && lock_args.l_type != F_UNLCK) {
			conflicting_lock->lock_length = lock_args.l_len;
			conflicting_lock->lock_start = lock_args.l_start;
			conflicting_lock->lock_type = lock_args.l_type;
		} else {
			conflicting_lock->lock_length = 0;
			conflicting_lock->lock_start = 0;
			conflicting_lock->lock_type = FSAL_NO_LOCK;
		}
	}

	/* Fall through (retval == 0) */

 err:

	if (ceph_fd)
		PTHREAD_RWLOCK_unlock(&ceph_fd->fdlock);

	if (closefd)
		(void) ceph_ll_close(myself->export->cmount, my_fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return ceph2fsal_error(retval);
}
#endif

#ifdef USE_FSAL_CEPH_LL_DELEGATION
static void ceph_deleg_cb(Fh *fh, void *vhdl)
{
	fsal_status_t fsal_status;
	struct fsal_obj_handle *obj_hdl = vhdl;
	struct ceph_handle *hdl =
		container_of(obj_hdl, struct ceph_handle, handle);
	struct gsh_buffdesc key = {
		.addr = &hdl->key,
		.len = sizeof(hdl->key)
	};

	LogDebug(COMPONENT_FSAL, "Recalling delegations on %p", hdl);

	fsal_status = up_async_delegrecall(general_fridge, hdl->up_ops, &key,
						NULL, NULL);
	if (FSAL_IS_ERROR(fsal_status))
		LogCrit(COMPONENT_FSAL,
			"Unable to queue delegrecall for 0x%p: %s",
			hdl, fsal_err_txt(fsal_status));
}

static fsal_status_t ceph_fsal_lease_op2(struct fsal_obj_handle *obj_hdl,
					 state_t *state, void *owner,
					 fsal_deleg_t deleg)
{
	struct ceph_handle *myself =
		container_of(obj_hdl, struct ceph_handle, handle);
	fsal_status_t status = {0, 0};
	int retval = 0;
	Fh *my_fd = NULL;
	unsigned int cmd;
	bool has_lock = false;
	bool closefd = false;
	bool bypass = false;
	fsal_openflags_t openflags = FSAL_O_READ;
	struct ceph_fd *ceph_fd = NULL;

	switch (deleg) {
	case FSAL_DELEG_NONE:
		cmd = CEPH_DELEGATION_NONE;
		break;
	case FSAL_DELEG_RD:
		cmd = CEPH_DELEGATION_RD;
		break;
	case FSAL_DELEG_WR:
		/* No write delegations (yet!) */
		return ceph2fsal_error(-ENOTSUP);
	default:
		LogCrit(COMPONENT_FSAL, "Unknown requested lease state");
		return ceph2fsal_error(-EINVAL);
	};

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (state) {
		ceph_fd = &container_of(state, struct ceph_state_fd,
					state)->ceph_fd;

		PTHREAD_RWLOCK_rdlock(&ceph_fd->fdlock);
	}

	/* Get a usable file descriptor */
	status = ceph_find_fd(&my_fd, obj_hdl, bypass, state, openflags,
			      &has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		LogCrit(COMPONENT_FSAL, "Unable to find fd for lease op");

		if (ceph_fd)
			PTHREAD_RWLOCK_unlock(&ceph_fd->fdlock);

		return status;
	}

	retval = ceph_ll_delegation(myself->export->cmount, my_fd, cmd,
				    ceph_deleg_cb, obj_hdl);

	if (ceph_fd)
		PTHREAD_RWLOCK_unlock(&ceph_fd->fdlock);

	if (closefd)
		(void) ceph_ll_close(myself->export->cmount, my_fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return ceph2fsal_error(retval);
}
#endif

/**
 * @brief Set attributes on an object
 *
 * This function sets attributes on an object.  Which attributes are
 * set is determined by attrib_set->valid_mask. The FSAL must manage bypass
 * or not of share reservations, and a state may be passed.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] state      state_t to use for this operation
 * @param[in] attrib_set Attributes to set
 *
 * @return FSAL status.
 */
static fsal_status_t ceph_fsal_setattr2(struct fsal_obj_handle *obj_hdl,
					bool bypass, struct state_t *state,
					struct attrlist *attrib_set)
{
	struct ceph_handle *myself =
		container_of(obj_hdl, struct ceph_handle, handle);
	fsal_status_t status = {0, 0};
	int rc = 0;
	bool has_lock = false;
	bool closefd = false;
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	/* Stat buffer */
	struct ceph_statx stx;
	/* Mask of attributes to set */
	uint32_t mask = 0;
	bool reusing_open_state_fd = false;

	if (attrib_set->valid_mask & ~CEPH_SETTABLE_ATTRIBUTES) {
		LogDebug(COMPONENT_FSAL,
			 "bad mask %"PRIx64" not settable %"PRIx64,
			 attrib_set->valid_mask,
			 attrib_set->valid_mask & ~CEPH_SETTABLE_ATTRIBUTES);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
		    "attrs ", attrib_set, false);

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE))
		attrib_set->mode &=
		    ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	/* Test if size is being set, make sure file is regular and if so,
	 * require a read/write file descriptor.
	 */
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_SIZE)) {
		if (obj_hdl->type != REGULAR_FILE) {
			LogFullDebug(COMPONENT_FSAL,
				     "Setting size on non-regular file");
			return fsalstat(ERR_FSAL_INVAL, EINVAL);
		}

		/* We don't actually need an open fd, we are just doing the
		 * share reservation checking, thus the NULL parameters.
		 */
		status = fsal_find_fd(NULL, obj_hdl, NULL, &myself->share,
				      bypass, state, FSAL_O_RDWR, NULL, NULL,
				      &has_lock, &closefd, false,
				      &reusing_open_state_fd);

		if (FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_FSAL,
				     "fsal_find_fd status=%s",
				     fsal_err_txt(status));
			goto out;
		}
	}

	memset(&stx, 0, sizeof(stx));

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_SIZE)) {
		mask |= CEPH_SETATTR_SIZE;
		stx.stx_size = attrib_set->filesize;
		LogDebug(COMPONENT_FSAL,
			     "setting size to %lu", stx.stx_size);
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE)) {
		mask |= CEPH_SETATTR_MODE;
		stx.stx_mode = fsal2unix_mode(attrib_set->mode);
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_OWNER)) {
		mask |= CEPH_SETATTR_UID;
		stx.stx_uid = attrib_set->owner;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_GROUP)) {
		mask |= CEPH_SETATTR_GID;
		stx.stx_gid = attrib_set->group;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_ATIME)) {
		mask |= CEPH_SETATTR_ATIME;
		stx.stx_atime = attrib_set->atime;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_ATIME_SERVER)) {
		struct timespec timestamp;

		mask |= CEPH_SETATTR_ATIME;
	#ifdef CEPH_SETATTR_ATIME_NOW
		mask |= CEPH_SETATTR_ATIME_NOW;
	#endif
		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0) {
			LogDebug(COMPONENT_FSAL,
				 "clock_gettime returned %s (%d)",
				 strerror(errno), errno);
			status = fsalstat(posix2fsal_error(errno), errno);
			goto out;
		}
		stx.stx_atime = timestamp;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MTIME)) {
		mask |= CEPH_SETATTR_MTIME;
		stx.stx_mtime = attrib_set->mtime;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MTIME_SERVER)) {
		struct timespec timestamp;

		mask |= CEPH_SETATTR_MTIME;
	#ifdef CEPH_SETATTR_MTIME_NOW
		mask |= CEPH_SETATTR_MTIME_NOW;
	#endif
		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0) {
			LogDebug(COMPONENT_FSAL,
				 "clock_gettime returned %s (%d)",
				 strerror(errno), errno);
			status = fsalstat(posix2fsal_error(errno), errno);
			goto out;
		}
		stx.stx_mtime = timestamp;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_CTIME)) {
		mask |= CEPH_SETATTR_CTIME;
		stx.stx_ctime = attrib_set->ctime;
	}

#ifdef CEPH_SETATTR_BTIME
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_CREATION)) {
		mask |= CEPH_SETATTR_BTIME;
		stx.stx_btime = attrib_set->creation;
	}
#endif

	rc = fsal_ceph_ll_setattr(export->cmount, myself->i, &stx, mask,
					op_ctx->creds);
	if (rc < 0) {
		LogDebug(COMPONENT_FSAL,
			 "setattrx returned %s (%d)",
			 strerror(-rc), -rc);
		goto out;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR4_SEC_LABEL)) {
		rc = fsal_ceph_ll_setxattr(export->cmount, myself->i,
				export->sec_label_xattr,
				attrib_set->sec_label.slai_data.slai_data_val,
				attrib_set->sec_label.slai_data.slai_data_len,
				0, op_ctx->creds);
		if (rc < 0) {
			status = ceph2fsal_error(rc);
			goto out;
		}
	}

	/* Success */
	status = fsalstat(ERR_FSAL_NO_ERROR, 0);
 out:

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}

/**
 * @brief Manage closing a file when a state is no longer needed.
 *
 * When the upper layers are ready to dispense with a state, this method is
 * called to allow the FSAL to close any file descriptors or release any other
 * resources associated with the state. A call to free_state should be assumed
 * to follow soon.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] state      state_t to use for this operation
 *
 * @return FSAL status.
 */

static fsal_status_t ceph_fsal_close2(struct fsal_obj_handle *obj_hdl,
				      struct state_t *state)
{
	fsal_status_t status = {0, 0};
	struct ceph_handle *myself =
		container_of(obj_hdl, struct ceph_handle, handle);
	struct ceph_fd *my_fd = &container_of(state, struct ceph_state_fd,
					      state)->ceph_fd;

	if (state->state_type == STATE_TYPE_SHARE ||
	    state->state_type == STATE_TYPE_NLM_SHARE ||
	    state->state_type == STATE_TYPE_9P_FID) {
		/* This is a share state, we must update the share counters */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		update_share_counters(&myself->share,
				      my_fd->openflags,
				      FSAL_O_CLOSED);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
	}

	/* Acquire state's fdlock to make sure no other thread
	 * is operating on the fd while we close it.
	 */
	PTHREAD_RWLOCK_wrlock(&my_fd->fdlock);
	status = ceph_close_my_fd(myself, my_fd);
	PTHREAD_RWLOCK_unlock(&my_fd->fdlock);

	return status;
}

/**
 * @brief Write wire handle
 *
 * This function writes a 'wire' handle to be sent to clients and
 * received from the.
 *
 * @param[in]     handle_pub  Handle to digest
 * @param[in]     output_type Type of digest requested
 * @param[in,out] fh_desc     Location/size of buffer for
 *                            digest/Length modified to digest length
 *
 * @return FSAL status.
 */

static fsal_status_t
ceph_fsal_handle_to_wire(const struct fsal_obj_handle *handle_pub,
		    uint32_t output_type,
		    struct gsh_buffdesc *fh_desc)
{
	/* The private 'full' object handle */
	const struct ceph_handle *handle =
		container_of(handle_pub, const struct ceph_handle, handle);

	switch (output_type) {
		/* Digested Handles */
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		if (fh_desc->len < sizeof(handle->key)) {
			LogMajor(COMPONENT_FSAL,
				 "digest_handle: space too small for handle.  Need %zu, have %zu",
				 sizeof(handle->key), fh_desc->len);
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		} else {
			if (handle->key.chk_fscid)
				fh_desc->len = sizeof(handle->key);
			else
				fh_desc->len = sizeof(handle->key.chk_vi);
			memcpy(fh_desc->addr, &handle->key, fh_desc->len);
		}
		break;

	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Give a hash key for file handle
 *
 * This function locates a unique hash key for a given file.
 *
 * @param[in]  handle_pub The file whose key is to be found
 * @param[out] fh_desc    Address and length of key
 */

static void
ceph_fsal_handle_to_key(struct fsal_obj_handle *handle_pub,
		   struct gsh_buffdesc *fh_desc)
{
	/* The private 'full' object handle */
	struct ceph_handle *handle =
		container_of(handle_pub, struct ceph_handle, handle);

	fh_desc->addr = &handle->key;
	fh_desc->len = sizeof(handle->key);
}

#ifdef USE_CEPH_LL_FALLOCATE
static fsal_status_t ceph_fsal_fallocate(struct fsal_obj_handle *obj_hdl,
					 state_t *state, uint64_t offset,
					 uint64_t length, bool allocate)
{
	struct ceph_handle *myself =
			container_of(obj_hdl, struct ceph_handle, handle);
	fsal_status_t status;
	int retval = 0;
	Fh *my_fd = NULL;
	bool has_lock = false;
	bool closefd = false;
	fsal_openflags_t openflags = FSAL_O_WRITE;
	struct ceph_export *export =
		container_of(op_ctx->fsal_export, struct ceph_export, export);
	struct ceph_fd *ceph_fd = NULL;

	/* Acquire state's fdlock to prevent OPEN upgrade closing the
	 * file descriptor while we use it.
	 */
	if (state) {
		ceph_fd = &container_of(state, struct ceph_state_fd,
					state)->ceph_fd;
		PTHREAD_RWLOCK_rdlock(&ceph_fd->fdlock);
	}

	/* Get a usable file descriptor */
	status = ceph_find_fd(&my_fd, obj_hdl, false, state,
			      openflags, &has_lock, &closefd, false);

	if (FSAL_IS_ERROR(status)) {
		LogDebug(COMPONENT_FSAL,
			 "find_fd failed %s", msg_fsal_err(status.major));
		goto out;
	}

	retval = ceph_ll_fallocate(export->cmount, my_fd,
				   allocate ? 0 :
				   FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
				   offset, length);
	if (retval < 0) {
		status = ceph2fsal_error(retval);
		goto out;
	}

	retval = ceph_ll_fsync(export->cmount, my_fd, false);
	if (retval < 0)
		status = ceph2fsal_error(retval);
 out:
	if (ceph_fd)
		PTHREAD_RWLOCK_unlock(&ceph_fd->fdlock);

	if (closefd)
		(void) ceph_ll_close(myself->export->cmount, my_fd);

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}
#endif

/**
 * @brief Override functions in ops vector
 *
 * This function overrides implemented functions in the ops vector
 * with versions for this FSAL.
 *
 * @param[in] ops Handle operations vector
 */

void handle_ops_init(struct fsal_obj_ops *ops)
{
	fsal_default_obj_ops_init(ops);

	ops->release = ceph_fsal_release;
	ops->merge = ceph_fsal_merge;
	ops->lookup = ceph_fsal_lookup;
	ops->mkdir = ceph_fsal_mkdir;
	ops->mknode = ceph_fsal_mknode;
	ops->readdir = ceph_fsal_readdir;
	ops->symlink = ceph_fsal_symlink;
	ops->readlink = ceph_fsal_readlink;
	ops->getattrs = ceph_fsal_getattrs;
	ops->link = ceph_fsal_link;
	ops->rename = ceph_fsal_rename;
	ops->unlink = ceph_fsal_unlink;
	ops->close = ceph_fsal_close;
	ops->handle_to_wire = ceph_fsal_handle_to_wire;
	ops->handle_to_key = ceph_fsal_handle_to_key;
	ops->open2 = ceph_fsal_open2;
	ops->status2 = ceph_fsal_status2;
	ops->reopen2 = ceph_fsal_reopen2;
	ops->read2 = ceph_fsal_read2;
	ops->write2 = ceph_fsal_write2;
	ops->commit2 = ceph_fsal_commit2;
#ifdef USE_FSAL_CEPH_SETLK
	ops->lock_op2 = ceph_fsal_lock_op2;
#endif
#ifdef USE_FSAL_CEPH_LL_DELEGATION
	ops->lease_op2 = ceph_fsal_lease_op2;
#endif
	ops->setattr2 = ceph_fsal_setattr2;
	ops->close2 = ceph_fsal_close2;
#ifdef CEPH_PNFS
	handle_ops_pnfs(ops);
#endif				/* CEPH_PNFS */
#ifdef USE_CEPH_LL_FALLOCATE
	ops->fallocate = ceph_fsal_fallocate;
#endif
}
