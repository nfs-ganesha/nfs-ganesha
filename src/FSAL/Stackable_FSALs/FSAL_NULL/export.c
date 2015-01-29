/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/* export.c
 * NULL FSAL export object
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
#include "gsh_list.h"
#include "config_parsing.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "nullfs_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"

/* helpers to/from other NULL objects
 */

struct fsal_staticfsinfo_t *nullfs_staticinfo(struct fsal_module *hdl);

/* export object methods
 */

static void release(struct fsal_export *exp_hdl)
{
	struct nullfs_fsal_export *myself;
	struct fsal_module *sub_fsal;

	myself = container_of(exp_hdl, struct nullfs_fsal_export, export);
	sub_fsal = myself->sub_export->fsal;

	/* Release the sub_export */
	myself->sub_export->exp_ops.release(myself->sub_export);
	fsal_put(sub_fsal);

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	gsh_free(myself);	/* elvis has left the building */
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	return next_ops.exp_ops.get_fs_dynamic_info(exp_hdl, obj_hdl,
						     infop);
}

static bool fs_supports(struct fsal_export *exp_hdl,
			fsal_fsinfo_options_t option)
{
	return next_ops.exp_ops.fs_supports(exp_hdl, option);
}

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_maxfilesize(exp_hdl);
}

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_maxread(exp_hdl);
}

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_maxwrite(exp_hdl);
}

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_maxlink(exp_hdl);
}

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_maxnamelen(exp_hdl);
}

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_maxpathlen(exp_hdl);
}

static struct timespec fs_lease_time(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_lease_time(exp_hdl);
}

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_acl_support(exp_hdl);
}

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_supported_attrs(exp_hdl);
}

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_umask(exp_hdl);
}

static uint32_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops.fs_xattr_access_rights(exp_hdl);
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
	return next_ops.exp_ops.get_quota(exp_hdl, filepath, quota_type,
					   pquota);
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	return next_ops.exp_ops.set_quota(exp_hdl, filepath, quota_type,
					   pquota, presquota);
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
	return next_ops.exp_ops.extract_handle(exp_hdl, in_type, fh_desc);
}

/* nullfs_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void nullfs_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = nullfs_lookup_path;
	ops->extract_handle = extract_handle;
	ops->create_handle = nullfs_create_handle;
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

struct nullfsal_args {
	struct subfsal_args subfsal;
};

static struct config_item sub_fsal_params[] = {
	CONF_ITEM_STR("name", 1, 10, NULL,
		      subfsal_args, name),
	CONFIG_EOL
};

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_RELAX_BLOCK("FSAL", sub_fsal_params,
			 noop_conf_init, subfsal_commit,
			 nullfsal_args, subfsal),
	CONFIG_EOL
};

static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.nullfs-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t nullfs_create_export(struct fsal_module *fsal_hdl,
				   void *parse_node,
				   const struct fsal_up_vector *up_ops)
{
	fsal_status_t expres;
	struct fsal_module *fsal_stack;
	struct nullfs_fsal_export *myself;
	struct nullfsal_args nullfsal;
	struct config_error_type err_type;
	int retval;

	/* process our FSAL block to get the name of the fsal
	 * underneath us.
	 */
	retval = load_config_from_node(parse_node,
				       &export_param,
				       &nullfsal,
				       true,
				       &err_type);
	if (retval != 0)
		return fsalstat(ERR_FSAL_INVAL, 0);
	fsal_stack = lookup_fsal(nullfsal.subfsal.name);
	if (fsal_stack == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "nullfs_create_export: failed to lookup for FSAL %s",
			 nullfsal.subfsal.name);
		return fsalstat(ERR_FSAL_INVAL, EINVAL);
	}

	myself = gsh_calloc(1, sizeof(struct nullfs_fsal_export));
	if (myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "Could not allocate memory for export %s",
			 op_ctx->export->fullpath);
		return fsalstat(ERR_FSAL_NOMEM, ENOMEM);
	}

	expres = fsal_stack->m_ops.create_export(fsal_stack,
						nullfsal.subfsal.fsal_node,
						up_ops);
	fsal_put(fsal_stack);
	if (FSAL_IS_ERROR(expres)) {
		LogMajor(COMPONENT_FSAL,
			 "Failed to call create_export on underlying FSAL %s",
			 nullfsal.subfsal.name);
		gsh_free(myself);
		return expres;
	}

	myself->sub_export = op_ctx->fsal_export;

	/* Init next_ops structure */
	/*** FIX ME!!!
	 * This structure had 3 mallocs that were never freed,
	 * and would leak for every export created.
	 * Now static to avoid the leak, the saved contents were
	 * never restored back to the original.
	 */

	memcpy(&next_ops.exp_ops,
	       &myself->sub_export->exp_ops,
	       sizeof(struct export_ops));
#ifdef EXPORT_OPS_INIT
	/*** FIX ME!!!
	 * Need to iterate through the lists to save and restore.
	 */
	memcpy(&next_ops.obj_ops,
	       myself->sub_export->obj_ops,
	       sizeof(struct fsal_obj_ops));
	memcpy(&next_ops.dsh_ops,
	       myself->sub_export->dsh_ops,
	       sizeof(struct fsal_dsh_ops));
#endif				/* EXPORT_OPS_INIT */
	next_ops.up_ops = up_ops;

	retval = fsal_export_init(&myself->export);
	if (retval) {
		gsh_free(myself);
		return fsalstat(posix2fsal_error(retval), retval);
	}
	nullfs_export_ops_init(&myself->export.exp_ops);
#ifdef EXPORT_OPS_INIT
	/*** FIX ME!!!
	 * Need to iterate through the lists to save and restore.
	 */
	nullfs_handle_ops_init(myself->export.obj_ops);
#endif				/* EXPORT_OPS_INIT */
	myself->export.up_ops = up_ops;
	myself->export.fsal = fsal_hdl;

	/* lock myself before attaching to the fsal.
	 * keep myself locked until done with creating myself.
	 */
	op_ctx->fsal_export = &myself->export;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
