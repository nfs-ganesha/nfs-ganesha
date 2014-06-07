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
 * @file FSAL_CEPH/export.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date Thu Jul  5 16:37:47 2012
 *
 * @brief Implementation of FSAL export functions for Ceph
 *
 * This file implements the Ceph specific functionality for the FSAL
 * export handle.
 */

#include <limits.h>
#include <stdint.h>
#include <sys/statvfs.h>
#include <cephfs/libcephfs.h>
#include "abstract_mem.h"
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "internal.h"

/**
 * @brief Clean up an export
 *
 * This function cleans up an export after the last reference is
 * released.
 *
 * @param[in,out] export The export to be released
 *
 * @retval ERR_FSAL_NO_ERROR on success.
 * @retval ERR_FSAL_BUSY if the export is in use.
 */

static void release(struct fsal_export *export_pub)
{
	/* The priate, expanded export */
	struct export *export = container_of(export_pub, struct export, export);

	deconstruct_handle(export->root);
	export->root = 0;

	fsal_detach_export(export->export.fsal, &export->export.exports);
	free_export_ops(&export->export);

	export->export.ops = NULL;
	ceph_shutdown(export->cmount);
	export->cmount = NULL;
	gsh_free(export);
	export = NULL;
}

/**
 * @brief Return a handle corresponding to a path
 *
 * This function looks up the given path and supplies an FSAL object
 * handle.  Because the root path specified for the export is a Ceph
 * style root as supplied to mount -t ceph of ceph-fuse (of the form
 * host:/path), we check to see if the path begins with / and, if not,
 * skip until we find one.
 *
 * @param[in]  export_pub The export in which to look up the file
 * @param[in]  path       The path to look up
 * @param[out] pub_handle The created public FSAL handle
 *
 * @return FSAL status.
 */

static fsal_status_t lookup_path(struct fsal_export *export_pub,
				 const char *path,
				 struct fsal_obj_handle **pub_handle)
{
	/* The 'private' full export handle */
	struct export *export = container_of(export_pub,
					     struct export,
					     export);
	/* The 'private' full object handle */
	struct handle *handle = NULL;
	/* The buffer in which to store stat info */
	struct stat st;
	/* FSAL status structure */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* Return code from Ceph */
	int rc = 0;
	/* Find the actual path in the supplied path */
	const char *realpath;
	struct Inode *i = NULL;

	if (*path != '/') {
		realpath = strchr(path, ':');
		if (realpath == NULL) {
			status.major = ERR_FSAL_INVAL;
			goto out;
		}
		if (*(++realpath) != '/') {
			status.major = ERR_FSAL_INVAL;
			goto out;
		}
	} else {
		realpath = path;
	}

	*pub_handle = NULL;

	if (strcmp(realpath, "/") == 0) {
		assert(export->root);
		*pub_handle = &export->root->handle;
		goto out;
	}

	rc = ceph_ll_walk(export->cmount, realpath, &i, &st);
	if (rc < 0) {
		status = ceph2fsal_error(rc);
		goto out;
	}

	rc = construct_handle(&st, i, export, &handle);
	if (rc < 0) {
		ceph_ll_put(export->cmount, i);
		status = ceph2fsal_error(rc);
		goto out;
	}

	*pub_handle = &handle->handle;

 out:
	return status;
}

/**
 * @brief Decode a digested handle
 *
 * This function decodes a previously digested handle.
 *
 * @param[in]  exp_handle  Handle of the relevant fs export
 * @param[in]  in_type  The type of digest being decoded
 * @param[out] fh_desc  Address and length of key
 */
static fsal_status_t extract_handle(struct fsal_export *exp_hdl,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc)
{
	switch (in_type) {
		/* Digested Handles */
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		/* wire handles */
		fh_desc->len = sizeof(vinodeno_t);
		break;
	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

#ifdef CEPH_PNFS

/**
 * @brief Create a FSAL data server handle from a wire handle
 *
 * This function creates a FSAL data server handle from a client
 * supplied "wire" handle.  This is also where validation gets done,
 * since PUTFH is the only operation that can return
 * NFS4ERR_BADHANDLE.
 *
 * @param[in]  export_pub The export in which to create the handle
 * @param[in]  desc       Buffer from which to create the file
 * @param[out] ds_pub     FSAL data server handle
 *
 * @return NFSv4.1 error codes.
 */
nfsstat4 create_ds_handle(struct fsal_export * const export_pub,
			  const struct gsh_buffdesc * const desc,
			  struct fsal_ds_handle ** const ds_pub)
{
	/* Full 'private' export structure */
	struct export *export = container_of(export_pub,
					     struct export,
					     export);
	/* Handle to be created */
	struct ds *ds = NULL;

	*ds_pub = NULL;

	if (desc->len != sizeof(struct ds_wire))
		return NFS4ERR_BADHANDLE;

	ds = gsh_calloc(1, sizeof(struct ds));

	if (ds == NULL)
		return NFS4ERR_SERVERFAULT;

	/* Connect lazily when a FILE_SYNC4 write forces us to, not
	   here. */

	ds->connected = false;

	memcpy(&ds->wire, desc->addr, desc->len);

	if (ds->wire.layout.fl_stripe_unit == 0) {
		gsh_free(ds);
		return NFS4ERR_BADHANDLE;
	}

	fsal_ds_handle_init(&ds->ds, export->export.ds_ops, export->fsal);

	*ds_pub = &ds->ds;

	return NFS4_OK;
}

#endif				/* CEPH_PNFS */

/**
 * @brief Create a handle object from a wire handle
 *
 * The wire handle is given in a buffer outlined by desc, which it
 * looks like we shouldn't modify.
 *
 * @param[in]  export_pub Public export
 * @param[in]  desc       Handle buffer descriptor
 * @param[out] pub_handle The created handle
 *
 * @return FSAL status.
 */
static fsal_status_t create_handle(struct fsal_export *export_pub,
				   struct gsh_buffdesc *desc,
				   struct fsal_obj_handle **pub_handle)
{
	/* Full 'private' export structure */
	struct export *export = container_of(export_pub,
					     struct export,
					     export);
	/* FSAL status to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* The FSAL specific portion of the handle received by the
	   client */
	vinodeno_t *vi = desc->addr;
	/* Ceph return code */
	int rc = 0;
	/* Stat buffer */
	struct stat st;
	/* Handle to be created */
	struct handle *handle = NULL;
	/* Inode pointer */
	struct Inode *i = NULL;

	*pub_handle = NULL;

	if (desc->len != sizeof(vinodeno_t)) {
		status.major = ERR_FSAL_INVAL;
		goto out;
	}

	i = ceph_ll_get_inode(export->cmount, *vi);
	if (!i)
		return ceph2fsal_error(-ESTALE);

	/* The ceph_ll_connectable_m should have populated libceph's
	   cache with all this anyway */
	rc = ceph_ll_getattr(export->cmount, i, &st, 0, 0);
	if (rc < 0)
		return ceph2fsal_error(rc);


	rc = construct_handle(&st, i, export, &handle);
	if (rc < 0) {
		ceph_ll_put(export->cmount, i);
		status = ceph2fsal_error(rc);
		goto out;
	}

	*pub_handle = &handle->handle;

 out:
	return status;
}

/**
 * @brief Get dynamic filesystem info
 *
 * This function returns dynamic filesystem information for the given
 * export.
 *
 * @param[in]  export_pub The public export handle
 * @param[out] info       The dynamic FS information
 *
 * @return FSAL status.
 */

static fsal_status_t get_fs_dynamic_info(struct fsal_export *export_pub,
					 struct fsal_obj_handle *obj_hdl,
					 fsal_dynamicfsinfo_t *info)
{
	/* Full 'private' export */
	struct export *export = container_of(export_pub, struct export, export);
	/* Return value from Ceph calls */
	int rc = 0;
	/* Filesystem stat */
	struct statvfs vfs_st;

	rc = ceph_ll_statfs(export->cmount, export->root->i, &vfs_st);

	if (rc < 0)
		return ceph2fsal_error(rc);

	memset(info, 0, sizeof(fsal_dynamicfsinfo_t));
	info->total_bytes = vfs_st.f_frsize * vfs_st.f_blocks;
	info->free_bytes = vfs_st.f_frsize * vfs_st.f_bfree;
	info->avail_bytes = vfs_st.f_frsize * vfs_st.f_bavail;
	info->total_files = vfs_st.f_files;
	info->free_files = vfs_st.f_ffree;
	info->avail_files = vfs_st.f_favail;
	info->time_delta.tv_sec = 1;
	info->time_delta.tv_nsec = 0;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Query the FSAL's capabilities
 *
 * This function queries the capabilities of an FSAL export.
 *
 * @param[in] export_pub The public export handle
 * @param[in] option     The option to check
 *
 * @retval true if the option is supported.
 * @retval false if the option is unsupported (or unknown).
 */

static bool fs_supports(struct fsal_export *export_pub,
			fsal_fsinfo_options_t option)
{
	switch (option) {
	case fso_no_trunc:
		return true;

	case fso_chown_restricted:
		return true;

	case fso_case_insensitive:
		return false;

	case fso_case_preserving:
		return true;

	case fso_link_support:
		return true;

	case fso_symlink_support:
		return true;

	case fso_lock_support:
		return false;

	case fso_lock_support_owner:
		return false;

	case fso_lock_support_async_block:
		return false;

	case fso_named_attr:
		return false;

	case fso_unique_handles:
		return true;

	case fso_cansettime:
		return true;

	case fso_homogenous:
		return true;

	case fso_auth_exportpath_xdev:
		return false;

	case fso_accesscheck_support:
		return false;

	case fso_share_support:
		return false;

	case fso_share_support_owner:
		return false;

	case fso_pnfs_ds_supported:
		return false;

	case fso_delegations:
		return false;

	case fso_reopen_method:
		return false;
	}

	return false;
}

/**
 * @brief Return the longest file supported
 *
 * This function returns the length of the longest file supported.
 *
 * @param[in] export_pub The public export
 *
 * @return UINT64_MAX.
 */

static uint64_t fs_maxfilesize(struct fsal_export *export_pub)
{
	return UINT64_MAX;
}

/**
 * @brief Return the longest read supported
 *
 * This function returns the length of the longest read supported.
 *
 * @param[in] export_pub The public export
 *
 * @return 4 mebibytes.
 */

static uint32_t fs_maxread(struct fsal_export *export_pub)
{
	return 0x400000;
}

/**
 * @brief Return the longest write supported
 *
 * This function returns the length of the longest write supported.
 *
 * @param[in] export_pub The public export
 *
 * @return 4 mebibytes.
 */

static uint32_t fs_maxwrite(struct fsal_export *export_pub)
{
	return 0x400000;
}

/**
 * @brief Return the maximum number of hard links to a file
 *
 * This function returns the maximum number of hard links supported to
 * any file.
 *
 * @param[in] export_pub The public export
 *
 * @return 1024.
 */

static uint32_t fs_maxlink(struct fsal_export *export_pub)
{
	/* Ceph does not like hard links.  See the anchor table
	   design.  We should fix this, but have to do it in the Ceph
	   core. */
	return 1024;
}

/**
 * @brief Return the maximum size of a Ceph filename
 *
 * This function returns the maximum filename length.
 *
 * @param[in] export_pub The public export
 *
 * @return UINT32_MAX.
 */

static uint32_t fs_maxnamelen(struct fsal_export *export_pub)
{
	/* Ceph actually supports filenames of unlimited length, at
	   least according to the protocol docs.  We may wish to
	   constrain this later. */
	return UINT32_MAX;
}

/**
 * @brief Return the maximum length of a Ceph path
 *
 * This function returns the maximum path length.
 *
 * @param[in] export_pub The public export
 *
 * @return UINT32_MAX.
 */

static uint32_t fs_maxpathlen(struct fsal_export *export_pub)
{
	/* Similarly unlimited int he protocol */
	return UINT32_MAX;
}

/**
 * @brief Return the lease time
 *
 * This function returns the lease time.
 *
 * @param[in] export_pub The public export
 *
 * @return five minutes.
 */

static struct timespec fs_lease_time(struct fsal_export *export_pub)
{
	struct timespec lease = { 300, 0 };

	return lease;
}

/**
 * @brief Return ACL support
 *
 * This function returns the export's ACL support.
 *
 * @param[in] export_pub The public export
 *
 * @return FSAL_ACLSUPPORT_DENY.
 */

static fsal_aclsupp_t fs_acl_support(struct fsal_export *export_pub)
{
	return FSAL_ACLSUPPORT_DENY;
}

/**
 * @brief Return the attributes supported by this FSAL
 *
 * This function returns the mask of attributes this FSAL can support.
 *
 * @param[in] export_pub The public export
 *
 * @return supported_attributes as defined in internal.c.
 */

static attrmask_t fs_supported_attrs(struct fsal_export *export_pub)
{
	return supported_attributes;
}

/**
 * @brief Return the mode under which the FSAL will create files
 *
 * This function returns the default mode on any new file created.
 *
 * @param[in] export_pub The public export
 *
 * @return 0600.
 */

static uint32_t fs_umask(struct fsal_export *export_pub)
{
	return 0600;
}

/**
 * @brief Return the mode for extended attributes
 *
 * This function returns the access mode applied to extended
 * attributes.  This seems a bit dubious
 *
 * @param[in] export_pub The public export
 *
 * @return 0644.
 */

static uint32_t fs_xattr_access_rights(struct fsal_export *export_pub)
{
	return 0644;
}

/**
 * @brief Set operations for exports
 *
 * This function overrides operations that we've implemented, leaving
 * the rest for the default.
 *
 * @param[in,out] ops Operations vector
 */

void export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = lookup_path;
	ops->extract_handle = extract_handle;
	ops->create_handle = create_handle;
#ifdef CEPH_PNFS
	ops->create_ds_handle = create_ds_handle;
#endif				/* CEPH_PNFS */
	ops->get_fs_dynamic_info = get_fs_dynamic_info;
	ops->fs_supports = fs_supports;
	ops->fs_maxfilesize = fs_maxfilesize;
	ops->fs_maxread = fs_maxread;
	ops->fs_maxwrite = fs_maxwrite;
	ops->fs_maxlink = fs_maxlink;
	ops->fs_maxnamelen = fs_maxnamelen;
	ops->fs_maxpathlen = fs_maxpathlen;
	ops->fs_lease_time = fs_lease_time;
	ops->fs_acl_support = fs_acl_support;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->fs_umask = fs_umask;
	ops->fs_xattr_access_rights = fs_xattr_access_rights;
#ifdef CEPH_PNFS
	export_ops_pnfs(ops);
#endif				/* CEPH_PNFS */
}
