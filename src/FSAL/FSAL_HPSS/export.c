/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * DISCLAIMER
 * ----------
 * This file is part of FSAL_HPSS.
 * FSAL HPSS provides the glue in-between FSAL API and HPSS CLAPI
 * You need to have HPSS installed to properly compile this file.
 *
 * Linkage/compilation/binding/loading/etc of HPSS licensed Software
 * must occur at the HPSS partner's or licensee's location.
 * It is not allowed to distribute this software as compiled or linked
 * binaries or libraries, as they include HPSS licensed material.
 * -------------
 */

/* export.c
 * VFS FSAL export object
 */

#include "config.h"

#include "fsal.h"
#include <string.h>
#include <sys/types.h>
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "hpss_methods.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "nfs_exports.h"

/*
 * VFS internal export
 */

struct hpss_fsal_export {
	struct fsal_export export;
	struct hpssfsal_export_context export_context;
};

/* helpers to/from other VFS objects
 */


struct hpssfsal_export_context *hpss_get_root_pvfs(struct fsal_export *exp_hdl)
{
	struct hpss_fsal_export *myself;

	myself = container_of(exp_hdl, struct hpss_fsal_export, export);
	return &myself->export_context;
}

/* export object methods
 */

void release(struct fsal_export *exp_hdl)
{
	struct hpss_fsal_export *myself;

	myself = container_of(exp_hdl, struct hpss_fsal_export, export);
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	free(myself);  /* elvis has left the building */
}

/**
 * FSAL_dynamic_fsinfo:
 * Return dynamic filesystem info such as
 * used size, free size, number of objects...
 *
 * \param filehandle (input):
 *        Handle of an object in the filesystem
 *        whom info is to be retrieved.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
* \param dynamicinfo (output):
 *        Pointer to the static info of the filesystem.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_FAULT        (NULL pointer as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */

static fsal_status_t hpss_get_dynamic_info(struct fsal_export *exp_hdl,
					   struct fsal_obj_handle *obj_hdl,
					   fsal_dynamicfsinfo_t *dynamicinfo)
{
	/* sanity checks. */
	if (!dynamicinfo || !exp_hdl || !obj_hdl)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* return dummy values... like HPSS do... */
	dynamicinfo->total_bytes = INT_MAX;
	dynamicinfo->free_bytes = INT_MAX;
	dynamicinfo->avail_bytes = INT_MAX;

	dynamicinfo->total_files = 20000000;
	dynamicinfo->free_files = 1000000;
	dynamicinfo->avail_files = 1000000;

	dynamicinfo->time_delta.tv_sec = 1;
	dynamicinfo->time_delta.tv_nsec = 0;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static bool hpss_fs_supports(struct fsal_export *exp_hdl,
			     fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

static uint64_t hpss_fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

static uint32_t hpss_fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

static uint32_t hpss_fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

static uint32_t hpss_fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

static uint32_t hpss_fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

static uint32_t hpss_fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

static struct timespec hpss_fs_lease_time(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_lease_time(info);
}

static fsal_aclsupp_t hpss_fs_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_acl_support(info);
}

static attrmask_t hpss_fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_supported_attrs(info);
}

static uint32_t hpss_fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

static uint32_t hpss_fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = hpss_staticinfo(exp_hdl->fsal);
	return fsal_xattr_access_rights(info);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t hpss_extract_handle(struct fsal_export *exp_hdl,
					 fsal_digesttype_t in_type,
					 struct gsh_buffdesc *fh_desc,
					 int flags)
{
	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	if (fh_desc->len != sizeof(ns_ObjHandle_t)) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %u",
			 (unsigned long int)sizeof(ns_ObjHandle_t),
			 (unsigned int)fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* hpss_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void hpss_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path =
		(fsal_status_t (*)(struct fsal_export *,
			const char *,
			struct fsal_obj_handle **))hpss_lookup_path;
	ops->extract_handle = hpss_extract_handle;
	ops->create_handle = hpss_create_handle;
	ops->get_fs_dynamic_info = hpss_get_dynamic_info;
	ops->fs_supports = hpss_fs_supports;
	ops->fs_maxfilesize = hpss_fs_maxfilesize;
	ops->fs_maxread = hpss_fs_maxread;
	ops->fs_maxwrite = hpss_fs_maxwrite;
	ops->fs_maxlink = hpss_fs_maxlink;
	ops->fs_maxnamelen = hpss_fs_maxnamelen;
	ops->fs_maxpathlen = hpss_fs_maxpathlen;
	ops->fs_lease_time = hpss_fs_lease_time;
	ops->fs_acl_support = hpss_fs_acl_support;
	ops->fs_supported_attrs = hpss_fs_supported_attrs;
	ops->fs_umask = hpss_fs_umask;
	ops->fs_xattr_access_rights = hpss_fs_xattr_access_rights;
}


static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONFIG_EOL
};

static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.hpss-export%d",
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

fsal_status_t hpss_create_export(struct fsal_module *fsal_hdl,
				 void *parse_node,
				 struct config_error_type *err_type,
				 const struct fsal_up_vector *up_ops)
{
	struct hpss_fsal_export *myself;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	myself = gsh_calloc(1, sizeof(struct hpss_fsal_export));

	retval = fsal_export_init(&myself->export);
	if (retval != 0)
		goto errout;

	hpss_export_ops_init(&myself->export.exp_ops);
	myself->export.up_ops = up_ops;

	retval = load_config_from_node(parse_node,
				       &export_param,
				       myself,
				       true,
				       err_type);

	if (retval != 0) {
		fsal_error = posix2fsal_error(retval);
		goto errout; /* seriously bad */
	}
	myself->export.fsal = fsal_hdl;

	op_ctx->fsal_export = &myself->export;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

errout:
	free(myself);  /* elvis has left the building */
	return fsalstat(fsal_error, retval);
}


