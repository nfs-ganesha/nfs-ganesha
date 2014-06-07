/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
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
 * PSEUDO FSAL export object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <os/mntent.h>
#include <os/quota.h>
#include <dlfcn.h>
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "pseudofs_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"

/* helpers to/from other PSEUDO objects
 */

struct fsal_staticfsinfo_t *pseudofs_staticinfo(struct fsal_module *hdl);

/* export object methods
 */

static void release(struct fsal_export *exp_hdl)
{
	struct pseudofs_fsal_export *myself;

	myself = container_of(exp_hdl, struct pseudofs_fsal_export, export);

	if (myself->root_handle != NULL) {
		fsal_obj_handle_uninit(&myself->root_handle->obj_handle);

		LogDebug(COMPONENT_FSAL,
			 "Releasing hdl=%p, name=%s",
			 myself->root_handle, myself->root_handle->name);

		if (myself->root_handle->name != NULL)
			gsh_free(myself->root_handle->name);

		gsh_free(myself->root_handle);
		myself->root_handle = NULL;
	}

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	if (myself->export_path != NULL)
		gsh_free(myself->export_path);

	gsh_free(myself);
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	infop->total_bytes = 0;
	infop->free_bytes = 0;
	infop->avail_bytes = 0;
	infop->total_files = 0;
	infop->free_files = 0;
	infop->avail_files = 0;
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static bool fs_supports(struct fsal_export *exp_hdl,
			fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

static struct timespec fs_lease_time(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_lease_time(info);
}

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_acl_support(info);
}

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_supported_attrs(info);
}

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

static uint32_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = pseudofs_staticinfo(exp_hdl->fsal);
	return fsal_xattr_access_rights(info);
}

/* get_quota
 * return quotas for this export.
 * path could cross a lower mount boundary which could
 * mask lower mount values with those of the export root
 * if this is a real issue, we can scan each time with setmntent()
 * better yet, compare st_dev of the file with st_dev of root_fd.
 * on linux, can map st_dev -> /proc/partitions name -> /dev/<name>
 */

static fsal_status_t get_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       fsal_quota_t *pquota)
{
	/* PSEUDOFS doesn't support quotas */
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	/* PSEUDOFS doesn't support quotas */
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t extract_handle(struct fsal_export *exp_hdl,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc)
{
	size_t fh_min;

	fh_min = 1;

	if (fh_desc->len < fh_min) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be >= %lu, got %lu",
			 fh_min, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* pseudofs_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void pseudofs_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = pseudofs_lookup_path;
	ops->extract_handle = extract_handle;
	ops->create_handle = pseudofs_create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
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
	ops->get_quota = get_quota;
	ops->set_quota = set_quota;
}

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t pseudofs_create_export(struct fsal_module *fsal_hdl,
				     void *parse_node,
				     const struct fsal_up_vector *up_ops)
{
	struct pseudofs_fsal_export *myself;
	int retval = 0;

	myself = gsh_calloc(1, sizeof(struct pseudofs_fsal_export));

	if (myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "Could not allocate export");
		return fsalstat(posix2fsal_error(errno), errno);
	}

	retval = fsal_export_init(&myself->export);

	if (retval != 0) {
		LogMajor(COMPONENT_FSAL,
			 "Could not initialize export");
		gsh_free(myself);
		return fsalstat(posix2fsal_error(retval), retval);
	}

	pseudofs_export_ops_init(myself->export.ops);
	pseudofs_handle_ops_init(myself->export.obj_ops);

	myself->export.up_ops = up_ops;

	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);

	if (retval != 0) {
		/* seriously bad */
		LogMajor(COMPONENT_FSAL,
			 "Could not attach export");
		goto errout;
	}

	myself->export.fsal = fsal_hdl;

	/* Save the export path. */
	myself->export_path = gsh_strdup(op_ctx->export->fullpath);

	if (myself->export_path == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Could not allocate export path");
		retval = ENOMEM;
		goto errout;
	}

	op_ctx->fsal_export = &myself->export;

	LogDebug(COMPONENT_FSAL,
		 "Created exp %p - %s",
		 myself, myself->export_path);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 errout:

	if (myself->export_path != NULL)
		gsh_free(myself->export_path);

	if (myself->root_handle != NULL)
		gsh_free(myself->root_handle);

	free_export_ops(&myself->export);

	gsh_free(myself);	/* elvis has left the building */

	return fsalstat(posix2fsal_error(retval), retval);
}
