/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat, 2015
 * Author: Orit Wasserman <owasserm@redhat.com>
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
 * -------------
 */

/* export.c
 * RGW FSAL export object
 */

#include <limits.h>
#include <stdint.h>
#include <sys/statvfs.h>
#include "abstract_mem.h"
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
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
	/* The private, expanded export */
	struct rgw_export *export =
	    container_of(export_pub, struct rgw_export, export);

	int rc = rgw_umount(export->rgw_fs, RGW_UMOUNT_FLAG_NONE);

	assert(rc == 0);
	deconstruct_handle(export->root);
	export->rgw_fs = NULL;
	export->root = NULL;

	fsal_detach_export(export->export.fsal, &export->export.exports);
	free_export_ops(&export->export);

	/* XXX we might need/want an rgw_unmount here, but presently,
	 * it wouldn't do anything */

	gsh_free(export);
	export = NULL;
}

/**
 * @brief Return a handle corresponding to a path
 *
 * This function looks up the given path and supplies an FSAL object
 * handle.
 *
 * @param[in]  export_pub The export in which to look up the file
 * @param[in]  path       The path to look up
 * @param[out] pub_handle The created public FSAL handle
 *
 * @return FSAL status.
 */

static fsal_status_t lookup_path(struct fsal_export *export_pub,
				 const char *path,
				 struct fsal_obj_handle **pub_handle,
				 struct attrlist *attrs_out)
{
	/* The 'private' full export handle */
	struct rgw_export *export =
	    container_of(export_pub, struct rgw_export, export);
	/* The 'private' full object handle */
	struct rgw_handle *handle = NULL;
	/* FSAL status structure */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* The buffer in which to store stat info */
	struct stat st;
	/* Return code from Ceph */
	int rc;
	/* temp filehandle */
	struct rgw_file_handle *rgw_fh;
	/* bucket name */
	const char *bucket_name = NULL;
	/* global directory */
	const char *global_dir = NULL;

	*pub_handle = NULL;

	/* pattern like "bucket_name/" or "bucket_name/dir/" should be avoid */
	if (strcmp(path, "/") && !strcmp((path + strlen(path) - 1), "/")) {
		status.major = ERR_FSAL_INVAL;
		return status;
	}

	/* should only be "/" or "bucket_name" or "bucket_name/dir "*/
	if (strcmp(path, "/") && strchr(path, '/') &&
		(strchr(path, '/') - path) > 1) {
		/* case : "bucket_name/dir" */
		char *cp_path = strdup(path);

		bucket_name = strsep(&cp_path, "/");
		global_dir = path + strlen(bucket_name) + 1;
	} else if (strcmp(path, "/") && strchr(path, '/') &&
		(strchr(path, '/') - path) == 0) {
		/* case : "/bucket_name" */
		bucket_name = path + 1;
	} else {
		/* case : "/" or "bucket_name" */
		bucket_name = path;
	}

	/* XXX in FSAL_CEPH, the equivalent code here looks for path == "/"
	 * and returns the root handle with no extra ref.  That seems
	 * suspicious, so let RGW figure it out (hopefully, that does not
	 * leak refs)
	 */
#ifndef USE_FSAL_RGW_MOUNT2
	if (global_dir == NULL) {
		rc = rgw_lookup(export->rgw_fs,
				export->rgw_fs->root_fh,
				bucket_name, &rgw_fh, RGW_LOOKUP_FLAG_NONE);
		if (rc < 0)
			return rgw2fsal_error(rc);
	}
#else
	if (global_dir == NULL) {
		rgw_fh = export->rgw_fs->root_fh;
	}
#endif
	if (global_dir != NULL) {
		/* search fh of bucket */
		struct rgw_file_handle *rgw_dh;

		rc = rgw_lookup(export->rgw_fs,
					export->rgw_fs->root_fh, bucket_name,
					&rgw_dh, RGW_LOOKUP_FLAG_NONE);

		if (rc < 0)
			return rgw2fsal_error(rc);
		/* search fh of global directory */
		rc = rgw_lookup(export->rgw_fs, rgw_dh, global_dir,
					&rgw_fh, RGW_LOOKUP_FLAG_RCB);
		if (rc < 0)
			return rgw2fsal_error(rc);
		if (rgw_fh->fh_type == RGW_FS_TYPE_FILE) {
			/* only directory can be an global fh */
			status.major = ERR_FSAL_INVAL;
			return status;
		}
	}

	/* get Unix attrs */
	if (global_dir == NULL) {
		rc = rgw_getattr(export->rgw_fs, export->rgw_fs->root_fh,
					&st, RGW_GETATTR_FLAG_NONE);
		if (rc < 0) {
			return rgw2fsal_error(rc);
		}
	} else {
		rc = rgw_getattr(export->rgw_fs, rgw_fh,
						&st, RGW_GETATTR_FLAG_NONE);
		if (rc < 0) {
			return rgw2fsal_error(rc);
		}
	}

#ifndef USE_FSAL_RGW_MOUNT2
	struct stat st_root;

	/* fixup export fsid */
	rc = rgw_getattr(export->rgw_fs, export->rgw_fs->root_fh,
			 &st_root, RGW_GETATTR_FLAG_NONE);
	if (rc < 0) {
		return rgw2fsal_error(rc);
	}
	st.st_dev = st_root.st_dev;
#endif
	rc = construct_handle(export, rgw_fh, &st, &handle);
	if (rc < 0) {
		return rgw2fsal_error(rc);
	}

	*pub_handle = &handle->handle;

	if (attrs_out != NULL) {
		posix2fsal_attributes_all(&st, attrs_out);
	}

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
static fsal_status_t wire_to_host(struct fsal_export *exp_hdl,
				  fsal_digesttype_t in_type,
				  struct gsh_buffdesc *fh_desc,
				  int flags)
{
	switch (in_type) {
		/* Digested Handles */
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		/* wire handles */
		fh_desc->len = sizeof(struct rgw_fh_hk);
		break;
	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

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
				   struct fsal_obj_handle **pub_handle,
				   struct attrlist *attrs_out)
{
	/* Full 'private' export structure */
	struct rgw_export *export =
	    container_of(export_pub, struct rgw_export, export);
	/* FSAL status to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* The FSAL specific portion of the handle received by the
	   client */
	int rc = 0;
	/* Stat buffer */
	struct stat st;
	/* Handle to be created */
	struct rgw_handle *handle = NULL;
	/* RGW fh hash key */
	struct rgw_fh_hk fh_hk;
	/* RGW file handle instance */
	struct rgw_file_handle *rgw_fh;

	*pub_handle = NULL;

	if (desc->len != sizeof(struct rgw_fh_hk)) {
		status.major = ERR_FSAL_INVAL;
		return status;
	}

	memcpy((char *) &fh_hk, desc->addr, desc->len);

	rc = rgw_lookup_handle(export->rgw_fs, &fh_hk, &rgw_fh,
			RGW_LOOKUP_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(-ESTALE);

	rc = rgw_getattr(export->rgw_fs, rgw_fh, &st, RGW_GETATTR_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	rc = construct_handle(export, rgw_fh, &st, &handle);
	if (rc < 0) {
		return rgw2fsal_error(rc);
	}

	*pub_handle = &handle->handle;

	if (attrs_out != NULL) {
		posix2fsal_attributes_all(&st, attrs_out);
	}

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
	struct rgw_export *export =
	    container_of(export_pub, struct rgw_export, export);

	int rc = 0;

	/* Filesystem stat */
	struct rgw_statvfs vfs_st;

	rc = rgw_statfs(export->rgw_fs, export->rgw_fs->root_fh, &vfs_st,
			RGW_STATFS_FLAG_NONE);
	if (rc < 0)
		return rgw2fsal_error(rc);

	/* TODO: implement in rgw_file */
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
	ops->wire_to_host = wire_to_host;
	ops->create_handle = create_handle;
	ops->get_fs_dynamic_info = get_fs_dynamic_info;
	ops->alloc_state = rgw_alloc_state;
}
