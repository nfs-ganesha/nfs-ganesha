// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright (C) 2019 Skytechnology sp. z o.o.
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <string.h>
#include <sys/statvfs.h>

#include "fsal_convert.h"
#include "fsal_types.h"
#include "FSAL/fsal_config.h"

#include "context_wrap.h"
#include "lzfs_internal.h"

/*! \brief Finalize an export
 *
 * \see fsal_api.h for more information
 */
static void lzfs_fsal_release(struct fsal_export *export_hdl)
{
	struct lzfs_fsal_export *lzfs_export;

	lzfs_export = container_of(export_hdl, struct lzfs_fsal_export,
				   export);

	lzfs_fsal_delete_handle(lzfs_export->root);
	lzfs_export->root = NULL;

	fsal_detach_export(lzfs_export->export.fsal,
			   &lzfs_export->export.exports);
	free_export_ops(&lzfs_export->export);

	if (lzfs_export->fileinfo_cache) {
		liz_reset_fileinfo_cache_params(lzfs_export->fileinfo_cache,
						0,
						0);

		while (1) {
			liz_fileinfo_entry_t *cache_handle;
			liz_fileinfo_t *file_handle;

			cache_handle = liz_fileinfo_cache_pop_expired(
						lzfs_export->fileinfo_cache);
			if (!cache_handle) {
				break;
			}
			file_handle = liz_extract_fileinfo(cache_handle);
			liz_release(lzfs_export->lzfs_instance, file_handle);
			liz_fileinfo_entry_free(cache_handle);
		}

		liz_destroy_fileinfo_cache(lzfs_export->fileinfo_cache);
		lzfs_export->fileinfo_cache = NULL;
	}

	liz_destroy(lzfs_export->lzfs_instance);
	lzfs_export->lzfs_instance = NULL;
	gsh_free((char *)lzfs_export->lzfs_params.subfolder);
	gsh_free(lzfs_export);
}

/*! \brief Look up a path
 *
 * \see fsal_api.h for more information
 */
static fsal_status_t lzfs_fsal_lookup_path(struct fsal_export *export_hdl,
					   const char *path,
					   struct fsal_obj_handle **pub_handle,
					   struct fsal_attrlist *attrs_out)
{
	static const char *root_dir_path = "/";

	struct lzfs_fsal_export *lzfs_export;
	struct lzfs_fsal_handle *lzfs_handle = NULL;
	const char *real_path;
	int rc;

	LogFullDebug(COMPONENT_FSAL, "export_id=%" PRIu16 " path=%s",
		     export_hdl->export_id, path);

	lzfs_export = container_of(export_hdl,
				   struct lzfs_fsal_export,
				   export);

	*pub_handle = NULL;

	// set the real_path to the path without the prefix from
	// ctx_export->fullpath
	if (*path != '/') {
		real_path = strchr(path, ':');
		if (real_path == NULL) {
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
		++real_path;
		if (*real_path != '/') {
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
	} else {
		real_path = path;
	}
	if (strstr(real_path, CTX_FULLPATH(op_ctx)) != real_path) {
		LogFullDebug(COMPONENT_FSAL, "no fullpath match");
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	real_path += strlen(CTX_FULLPATH(op_ctx));
	if (*real_path == '\0') {
		real_path = root_dir_path;
	}

	LogFullDebug(COMPONENT_FSAL, "real_path=%s", real_path);

	// special case the root
	if (strcmp(real_path, "/") == 0) {
		assert(lzfs_export->root);
		*pub_handle = &lzfs_export->root->handle;
		if (attrs_out == NULL) {
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		}
	}

	liz_entry_t result;

	rc = liz_cred_lookup(lzfs_export->lzfs_instance, &op_ctx->creds,
			     SPECIAL_INODE_ROOT, real_path, &result);

	if (rc < 0) {
		return lzfs_fsal_last_err();
	}

	if (attrs_out != NULL) {
		posix2fsal_attributes_all(&result.attr, attrs_out);
	}

	if (*pub_handle == NULL) {
		lzfs_handle = lzfs_fsal_new_handle(&result.attr, lzfs_export);
		*pub_handle = &lzfs_handle->handle;
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*! \brief Convert a wire handle to a host handle
 *
 * \see fsal_api.h for more information
 */
static fsal_status_t lzfs_fsal_wire_to_host(struct fsal_export *exp_hdl,
					    fsal_digesttype_t in_type,
					    struct gsh_buffdesc *fh_desc,
					    int flags)
{
	liz_inode_t *inode;

	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	inode = (liz_inode_t *)fh_desc->addr;
	if (flags & FH_FSAL_BIG_ENDIAN) {
#if (BYTE_ORDER != BIG_ENDIAN)
		assert(sizeof(liz_inode_t) == 4);
		*inode = bswap_32(*inode);
#endif
	} else {
#if (BYTE_ORDER == BIG_ENDIAN)
		assert(sizeof(liz_inode_t) == 4);
		*inode = bswap_32(*inode);
#endif
	}

	if (fh_desc->len != sizeof(liz_inode_t)) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle. Should be %zu, got %zu",
			 sizeof(liz_inode_t), fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * \brief Create a FSAL object handle from a host handle
 *
 * \see fsal_api.h for more information
 */
static fsal_status_t lzfs_fsal_create_handle(
					struct fsal_export *exp_hdl,
					struct gsh_buffdesc *desc,
					struct fsal_obj_handle **pub_handle,
					struct fsal_attrlist *attrs_out)
{
	struct lzfs_fsal_export *lzfs_export;
	struct lzfs_fsal_handle *handle = NULL;
	liz_inode_t *inode;
	int rc;

	lzfs_export = container_of(exp_hdl, struct lzfs_fsal_export, export);
	inode = (liz_inode_t *)desc->addr;

	*pub_handle = NULL;
	if (desc->len != sizeof(liz_inode_t)) {
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	liz_attr_reply_t result;

	rc = liz_cred_getattr(lzfs_export->lzfs_instance, &op_ctx->creds,
			      *inode, &result);

	if (rc < 0) {
		return lzfs_fsal_last_err();
	}

	handle = lzfs_fsal_new_handle(&result.attr, lzfs_export);

	if (attrs_out != NULL) {
		posix2fsal_attributes_all(&result.attr, attrs_out);
	}

	*pub_handle = &handle->handle;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*! \brief Get filesystem statistics
 *
 * \see fsal_api.h for more information
 */
static fsal_status_t lzfs_fsal_get_fs_dynamic_info(
					struct fsal_export *exp_hdl,
					struct fsal_obj_handle *obj_hdl,
					fsal_dynamicfsinfo_t *info)
{
	struct lzfs_fsal_export *lzfs_export;
	int rc;

	lzfs_export = container_of(exp_hdl, struct lzfs_fsal_export, export);

	liz_stat_t st;

	rc = liz_statfs(lzfs_export->lzfs_instance, &st);
	if (rc < 0) {
		return lzfs_fsal_last_err();
	}

	memset(info, 0, sizeof(fsal_dynamicfsinfo_t));
	info->total_bytes = st.total_space;
	info->free_bytes = st.avail_space;
	info->avail_bytes = st.avail_space;
	info->total_files = MAX_REGULAR_INODE;
	info->free_files = MAX_REGULAR_INODE - st.inodes;
	info->avail_files = MAX_REGULAR_INODE - st.inodes;
	info->time_delta.tv_sec = 0;
	info->time_delta.tv_nsec = FSAL_DEFAULT_TIME_DELTA_NSEC;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/*! \brief Export feature test
 *
 * \see fsal_api.h for more information
 */
static bool lzfs_fsal_fs_supports(struct fsal_export *exp_hdl,
				  fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = lzfs_fsal_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

/*! \brief Get the greatest file size supported
 *
 * \see fsal_api.h for more information
 */
static uint64_t lzfs_fsal_fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lzfs_fsal_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

/*! \brief Get the greatest read size supported
 *
 * \see fsal_api.h for more information
 */
static uint32_t lzfs_fsal_fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lzfs_fsal_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

/*! \brief Get the greatest write size supported
 *
 * \see fsal_api.h for more information
 */
static uint32_t lzfs_fsal_fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lzfs_fsal_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

/*! \brief Get the greatest link count supported
 *
 * \see fsal_api.h for more information
 */
static uint32_t lzfs_fsal_fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lzfs_fsal_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

/*! \brief Get the greatest name length supported
 *
 * \see fsal_api.h for more information
 */
static uint32_t lzfs_fsal_fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lzfs_fsal_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

/*! \brief Get the greatest path length supported
 *
 * \see fsal_api.h for more information
 */
static uint32_t lzfs_fsal_fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lzfs_fsal_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

/*! \brief Get supported ACL types
 *
 * \see fsal_api.h for more information
 */
static fsal_aclsupp_t lzfs_fsal_fs_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lzfs_fsal_staticinfo(exp_hdl->fsal);
	return fsal_acl_support(info);
}

/*! \brief Get supported attributes
 *
 * \see fsal_api.h for more information
 */
static attrmask_t lzfs_fsal_fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;
	attrmask_t supported_mask;

	info = lzfs_fsal_staticinfo(exp_hdl->fsal);

	supported_mask = fsal_supported_attrs(info);

	return supported_mask;
}

/*! \brief Get umask applied to created files
 *
 * \see fsal_api.h for more information
 */
static uint32_t lzfs_fsal_fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = lzfs_fsal_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

/*! \brief Allocate a state_t structure
 *
 * \see fsal_api.h for more information
 */
struct state_t *lzfs_fsal_alloc_state(struct fsal_export *exp_hdl,
				      enum state_type state_type,
				      struct state_t *related_state)
{
	return init_state(gsh_calloc(1, sizeof(struct lzfs_fsal_state_fd)),
			  exp_hdl, state_type, related_state);
}

/*! \brief Free a state_t structure
 *
 * \see fsal_api.h for more information
 */
void lzfs_fsal_free_state(struct fsal_export *exp_hdl, struct state_t *state)
{
	struct lzfs_fsal_state_fd *state_fd = container_of(
				state, struct lzfs_fsal_state_fd, state);
	gsh_free(state_fd);
}

void lzfs_fsal_export_ops_init(struct export_ops *ops)
{
	ops->release = lzfs_fsal_release;
	ops->lookup_path = lzfs_fsal_lookup_path;
	ops->wire_to_host = lzfs_fsal_wire_to_host;
	ops->create_handle = lzfs_fsal_create_handle;
	ops->get_fs_dynamic_info = lzfs_fsal_get_fs_dynamic_info;
	ops->fs_supports = lzfs_fsal_fs_supports;
	ops->fs_maxfilesize = lzfs_fsal_fs_maxfilesize;
	ops->fs_maxread = lzfs_fsal_fs_maxread;
	ops->fs_maxwrite = lzfs_fsal_fs_maxwrite;
	ops->fs_maxlink = lzfs_fsal_fs_maxlink;
	ops->fs_maxnamelen = lzfs_fsal_fs_maxnamelen;
	ops->fs_maxpathlen = lzfs_fsal_fs_maxpathlen;
	ops->fs_acl_support = lzfs_fsal_fs_acl_support;
	ops->fs_supported_attrs = lzfs_fsal_fs_supported_attrs;
	ops->fs_umask = lzfs_fsal_fs_umask;
	ops->alloc_state = lzfs_fsal_alloc_state;
	ops->free_state = lzfs_fsal_free_state;
	lzfs_fsal_export_ops_pnfs(ops);
}
