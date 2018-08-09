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
 * @addtogroup FSAL
 * @{
 */

/**
 * @file default_methods.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief System wide default FSAL methods
 */

#include "config.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "config_parsing.h"
#include "gsh_types.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_private.h"
#include "pnfs_utils.h"
#include "nfs_creds.h"
#include "sal_data.h"
#include "FSAL/fsal_config.h"

/** fsal module method defaults and common methods
 */

/** NOTE
 * These are the common and default methods.  One important requirement
 * is that older fsals must safely run with newer ganesha core.
 * This is observed by the following rules:
 *
 * 1. New methods are *always* appended to the m_ops vector in fsal_api.h
 *
 * 2. This file is updated to add the default method.
 *
 * 3. The version numbers are bumped in fsal_api.h appropriately so
 *    version detection is correct.
 */

/* unload fsal
 * called while holding the last remaining reference
 * remove from list and dlclose the module
 * if references are held, return EBUSY
 * if it is a static, return EACCES
 */

static int unload_fsal(struct fsal_module *fsal_hdl)
{
	int retval = EBUSY;	/* someone still has a reference */
	int32_t refcount = atomic_fetch_int32_t(&fsal_hdl->refcount);

	LogDebug(COMPONENT_FSAL,
		 "refcount = %"PRIi32,
		 refcount);

	PTHREAD_MUTEX_lock(&fsal_lock);

	if (refcount != 0 || !glist_empty(&fsal_hdl->exports)) {
		LogCrit(COMPONENT_FSAL,
			"Can not unload FSAL %s refcount=%"PRIi32,
			fsal_hdl->name, refcount);
		goto err;
	}
	if (fsal_hdl->dl_handle == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Can not unload static linked FSAL %s",
			fsal_hdl->name);
		retval = EACCES;
		goto err;
	}

	glist_del(&fsal_hdl->fsals);
	PTHREAD_RWLOCK_destroy(&fsal_hdl->lock);

	retval = dlclose(fsal_hdl->dl_handle);
	PTHREAD_MUTEX_unlock(&fsal_lock);
	return retval;

 err:
	PTHREAD_RWLOCK_unlock(&fsal_hdl->lock);
	PTHREAD_MUTEX_unlock(&fsal_lock);
	return retval;
}

/* init_config
 * default case is we have no config so return happy
 */

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* dump_config
 * default is to do nothing
 */

static void dump_config(struct fsal_module *fsal_hdl, int log_fd)
{
	/* return */
}

/* create_export
 * default is we cannot create an export
 */

static fsal_status_t create_export(struct fsal_module *fsal_hdl,
				   void *parse_node,
				   struct config_error_type *err_type,
				   const struct fsal_up_vector *up_ops)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/**
 * @brief Default emergency cleanup method
 *
 * Do nothing.
 */

static void emergency_cleanup(void)
{
	/* return */
}

/**
 * @brief Be uninformative about a device
 */

static nfsstat4 getdeviceinfo(struct fsal_module *fsal_hdl, XDR *da_addr_body,
			      const layouttype4 type,
			      const struct pnfs_deviceid *deviceid)
{
	return NFS4ERR_NOTSUPP;
}

/**
 * No da_addr.
 */

static size_t fs_da_addr_size(struct fsal_module *fsal_hdl)
{
	return 0;
}

/**
 * @brief Try to create a FSAL pNFS data server
 *
 * @param[in]  fsal_hdl		FSAL module
 * @param[in]  parse_node	opaque pointer to parse tree node for
 *				export options to be passed to
 *				load_config_from_node
 * @param[out] handle		FSAL pNFS DS
 *
 * @return FSAL status.
 */

static fsal_status_t fsal_pnfs_ds(struct fsal_module *const fsal_hdl,
				  void *parse_node,
				  struct fsal_pnfs_ds **const handle)
{
	LogDebug(COMPONENT_PNFS, "Default pNFS DS creation!");
	if (*handle == NULL)
		*handle = pnfs_ds_alloc();

	fsal_pnfs_ds_init(*handle, fsal_hdl);
	op_ctx->fsal_pnfs_ds = *handle;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Initialize FSAL specific values for pNFS data server
 *
 * @param[in]  ops	FSAL pNFS Data Server operations vector
 */

static void fsal_pnfs_ds_ops(struct fsal_pnfs_ds_ops *ops)
{
	memcpy(ops, &def_pnfs_ds_ops, sizeof(struct fsal_pnfs_ds_ops));
}

/**
 * @brief Provides function to extract FSAL stats
 *
 * @param[in] fsal_hdl          FSAL module
 * @param[in] iter              opaque pointer to DBusMessageIter
 */
static void fsal_extract_stats(struct fsal_module *const fsal_hdl, void *iter)
{
	LogDebug(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
}

/**
 * @brief FSAL function to reset FSAL stats
 *
 * @param[in] fsal_hdl          FSAL module
 */
static void fsal_reset_stats(struct fsal_module *const fsal_hdl)
{
	LogDebug(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
}

/* Default fsal module method vector.
 * copied to allocated vector at register time
 */

struct fsal_ops def_fsal_ops = {
	.unload = unload_fsal,
	.init_config = init_config,
	.dump_config = dump_config,
	.create_export = create_export,
	.emergency_cleanup = emergency_cleanup,
	.getdeviceinfo = getdeviceinfo,
	.fs_da_addr_size = fs_da_addr_size,
	.fsal_pnfs_ds = fsal_pnfs_ds,
	.fsal_pnfs_ds_ops = fsal_pnfs_ds_ops,
	.fsal_extract_stats = fsal_extract_stats,
	.fsal_reset_stats = fsal_reset_stats,
};

/* get_name
 * default case is to return the name of the FSAL
 */

static const char *get_name(struct fsal_export *exp_hdl)
{
	return exp_hdl->fsal->name;
}

/* export_prepare_unexport
 * Nothing to do in the default case
 */

static void export_prepare_unexport(struct fsal_export *exp_hdl)
{
	/* return */
}


/* export_unexport
 * Nothing to do in the default case
 */

static void export_unexport(struct fsal_export *exp_hdl,
			    struct fsal_obj_handle *root_obj)
{
	/* return */
}

/* export_release
 * Nothing to do in the default case
 */

static void export_release(struct fsal_export *exp_hdl)
{
	/* return */
}

/* lookup_path
 * default case is not supported
 */

fsal_status_t lookup_path(struct fsal_export *exp_hdl,
			  const char *path,
			  struct fsal_obj_handle **handle,
			  struct attrlist *attrs_out)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

static fsal_status_t lookup_junction(struct fsal_export *exp_hdl,
				     struct fsal_obj_handle *junction,
				     struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* wire_to_host
 * default case is not supported
 */

static fsal_status_t wire_to_host(struct fsal_export *exp_hdl,
				  fsal_digesttype_t in_type,
				  struct gsh_buffdesc *fh_desc,
				  int flags)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* exp_host_to_key  Produce handle-key from host-handle
 *
 * default case is that "handle-key" is same as host-handle!
 */
static fsal_status_t exp_host_to_key(struct fsal_export *exp_hdl,
				       struct gsh_buffdesc *fh_desc)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* create_handle
 * default case is not supported
 */

static fsal_status_t create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle,
				   struct attrlist *attrs_out)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* get_dynamic_info
 * default case is not supported
 */

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/**
 * @brief Query the FSAL's capabilities
 *
 * This function queries the capabilities of an FSAL export.
 *
 * @param[in] exp_hdl The public export handle
 * @param[in] option     The option to check
 *
 * @retval true if the option is supported.
 * @retval false if the option is unsupported (or unknown).
 */
static bool fs_supports(struct fsal_export *exp_hdl,
			fsal_fsinfo_options_t option)
{
	return fsal_supports(&exp_hdl->fsal->fs_info, option);
}

/**
 * @brief Return the longest file supported
 *
 * This function returns the length of the longest file supported.
 *
 * @param[in] exp_hdl The public export
 *
 */
static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	return fsal_maxfilesize(&exp_hdl->fsal->fs_info);
}

/**
 * @brief Return the longest read supported
 *
 * This function returns the length of the longest read supported.
 *
 * @param[in] exp_hdl The public export
 *
 */
static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	return fsal_maxread(&exp_hdl->fsal->fs_info);
}

/**
 * @brief Return the longest write supported
 *
 * This function returns the length of the longest write supported.
 *
 * @param[in] exp_hdl The public export
 *
 */
static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	return fsal_maxwrite(&exp_hdl->fsal->fs_info);
}

/**
 * @brief Return the maximum number of hard links to a file
 *
 * This function returns the maximum number of hard links supported to
 * any file.
 *
 * @param[in] exp_hdl The public export
 *
 */
static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	return fsal_maxlink(&exp_hdl->fsal->fs_info);
}

/**
 * @brief Return the maximum size of a Ceph filename
 *
 * This function returns the maximum filename length.
 *
 * @param[in] exp_hdl The public export
 *
 */
static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	return fsal_maxnamelen(&exp_hdl->fsal->fs_info);
}


/**
 * @brief Return the maximum length of a Ceph path
 *
 * This function returns the maximum path length.
 *
 * @param[in] exp_hdl The public export
 *
 */
static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	return fsal_maxpathlen(&exp_hdl->fsal->fs_info);
}

/**
 * @brief Return ACL support
 *
 * This function returns the export's ACL support.
 *
 * @param[in] export_pub The public export
 *
 */
static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	return fsal_acl_support(&exp_hdl->fsal->fs_info);
}

/**
 * @brief Return the attributes supported by this FSAL
 *
 * This function returns the mask of attributes this FSAL can support.
 *
 * @param[in] exp_hdl The public export
 *
 */
static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	return fsal_supported_attrs(&exp_hdl->fsal->fs_info);
}

/**
 * @brief Return the mode under which the FSAL will create files
 *
 * This function modifies the default mode on any new file created.
 *
 * @param[in] export_pub The public export
 *
 * @return 0 (usually).  Bits set here turn off bits in created files.
 */
static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	return fsal_umask(&exp_hdl->fsal->fs_info);
}

/* check_quota
 * return happiness for now.
 */

static fsal_status_t check_quota(struct fsal_export *exp_hdl,
				 const char *filepath, int quota_type)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* get_quota
 * default case not supported
 */

static fsal_status_t get_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       int quota_id,
			       fsal_quota_t *pquota)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* set_quota
 * default case not supported
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       int quota_id,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/**
 * @brief Be uninformative about all devices
 */

static nfsstat4 getdevicelist(struct fsal_export *exp_hdl, layouttype4 type,
			      void *opaque, bool(*cb) (void *opaque,
						       const uint64_t id),
			      struct fsal_getdevicelist_res *res)
{
	return NFS4ERR_NOTSUPP;
}

/**
 * @brief Support no layout types
 */

static void fs_layouttypes(struct fsal_export *exp_hdl, int32_t *count,
			   const layouttype4 **types)
{
	*count = 0;
	*types = NULL;
}

/**
 * @brief Read no bytes through layouts
 */

static uint32_t fs_layout_blocksize(struct fsal_export *exp_hdl)
{
	return 0;
}

/**
 * @brief No segments
 */

static uint32_t fs_maximum_segments(struct fsal_export *exp_hdl)
{
	return 0;
}

/**
 * @brief No loc_body
 */

static size_t fs_loc_body_size(struct fsal_export *exp_hdl)
{
	return 0;
}

/**
 * @brief Get write verifier
 *
 * This function is called by write and commit to match the commit verifier
 * with the one returned on  write.
 *
 * @param[in,out] verf_desc Address and length of verifier
 *
 * @return No errors
 */
static void global_verifier(struct fsal_export *exp_hdl,
			    struct gsh_buffdesc *verf_desc)
{
	memcpy(verf_desc->addr, &NFS4_write_verifier, verf_desc->len);
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

static struct state_t *alloc_state(struct fsal_export *exp_hdl,
				   enum state_type state_type,
				   struct state_t *related_state)
{
	return init_state(gsh_calloc(1, sizeof(struct state_t)),
			  exp_hdl, state_type, related_state);
}

/**
 * @brief Free a state_t structure
 *
 * @param[in] state                 state_t structure to free.
 *
 * @returns NULL on failure otherwise a state structure.
 */

void free_state(struct fsal_export *exp_hdl, struct state_t *state)
{
	gsh_free(state);
}

/**
 * @brief Check to see if a user is superuser
 *
 * @param[in] exp_hdl               Export state_t is associated with
 * @param[in] creds                 Credentials to check for superuser
 *
 * @returns NULL on failure otherwise a state structure.
 */

bool is_superuser(struct fsal_export *exp_hdl, const struct user_cred *creds)
{
	return (creds->caller_uid == 0);
}

/* Default fsal export method vector.
 * copied to allocated vector at register time
 */

struct export_ops def_export_ops = {
	.get_name = get_name,
	.prepare_unexport = export_prepare_unexport,
	.unexport = export_unexport,
	.release = export_release,
	.lookup_path = lookup_path,
	.lookup_junction = lookup_junction,
	.wire_to_host = wire_to_host,
	.host_to_key = exp_host_to_key,
	.create_handle = create_handle,
	.get_fs_dynamic_info = get_dynamic_info,
	.fs_supports = fs_supports,
	.fs_maxfilesize = fs_maxfilesize,
	.fs_maxread = fs_maxread,
	.fs_maxwrite = fs_maxwrite,
	.fs_maxlink = fs_maxlink,
	.fs_maxnamelen = fs_maxnamelen,
	.fs_maxpathlen = fs_maxpathlen,
	.fs_acl_support = fs_acl_support,
	.fs_supported_attrs = fs_supported_attrs,
	.fs_umask = fs_umask,
	.check_quota = check_quota,
	.get_quota = get_quota,
	.set_quota = set_quota,
	.getdevicelist = getdevicelist,
	.fs_layouttypes = fs_layouttypes,
	.fs_layout_blocksize = fs_layout_blocksize,
	.fs_maximum_segments = fs_maximum_segments,
	.fs_loc_body_size = fs_loc_body_size,
	.get_write_verifier = global_verifier,
	.alloc_state = alloc_state,
	.free_state = free_state,
	.is_superuser = is_superuser,
};

/* fsal_obj_handle common methods
 */

/* get_ref
 * This MUST be handled by someone. For many FSALs, it is handled by
 * FSAL_MDCACHE.
 */

static void handle_get_ref(struct fsal_obj_handle *obj_hdl)
{
	/* return */
}

/* put_ref
 * This MUST be handled by someone. For many FSALs, it is handled by
 * FSAL_MDCACHE.
 */

static void handle_put_ref(struct fsal_obj_handle *obj_hdl)
{
	/* return */
}

/* handle_release
 * default case is to throw a fault error.
 * creating an handle is not supported so getting here is bad
 */

static void handle_release(struct fsal_obj_handle *obj_hdl)
{
	/* return */
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

static fsal_status_t handle_merge(struct fsal_obj_handle *orig_hdl,
				  struct fsal_obj_handle *dupe_hdl)
{
	/* Default is to do nothing. */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* lookup
 * default case not supported
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const char *path, struct fsal_obj_handle **handle,
			    struct attrlist *attrs_out)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* read_dirents
 * default case not supported
 */

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, attrmask_t attrmask,
				  bool *eof)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* compute_readdir_cookie
 * default is to return 0 which indicates not supported
 */

fsal_cookie_t compute_readdir_cookie(struct fsal_obj_handle *parent,
				     const char *name)
{
	return 0;
}

/* dirent_cmp
 * Sort dirents by name.
 */

int dirent_cmp(struct fsal_obj_handle *parent,
	       const char *name1, fsal_cookie_t cookie1,
	       const char *name2, fsal_cookie_t cookie2)
{
	/* Not supported by default. */
	assert(false);
	return 0;
}

/* makedir
 * default case not supported
 */

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name, struct attrlist *attrib,
			     struct fsal_obj_handle **handle,
			     struct attrlist *attrs_out)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* makenode
 * default case not supported
 */

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
			      const char *name, object_file_type_t nodetype,
			      struct attrlist *attrib,
			      struct fsal_obj_handle **handle,
			      struct attrlist *attrs_out)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* makesymlink
 * default case not supported
 */

static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
				 const char *name, const char *link_path,
				 struct attrlist *attrib,
				 struct fsal_obj_handle **handle,
				 struct attrlist *attrs_out)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* readsymlink
 * default case not supported
 */

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* getxattrs
 * default case not supported
 */

static fsal_status_t getxattrs(struct fsal_obj_handle *obj_hdl,
				xattrname4 *xa_name,
				xattrvalue4 *xa_value)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* setxattrs
 * default case not supported
 */

static fsal_status_t setxattrs(struct fsal_obj_handle *obj_hdl,
				setxattr_type4 sa_type,
				xattrname4 *xa_name,
				xattrvalue4 *xa_value)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* removexattrs
 * default case not supported
 */

static fsal_status_t removexattrs(struct fsal_obj_handle *obj_hdl,
				xattrname4 *xa_name)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* listxattrs
 * default case not supported
 */

static fsal_status_t listxattrs(struct fsal_obj_handle *obj_hdl,
				count4 la_maxcount,
				nfs_cookie4 *la_cookie,
				verifier4 *la_cookieverf,
				bool_t *lr_eof,
				xattrlist4 *lr_names)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* getattrs
 * default case not supported
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* linkfile
 * default case not supported
 */

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* renamefile
 * default case not supported
 */

static fsal_status_t renamefile(struct fsal_obj_handle *obj_hdl,
				struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* file_unlink
 * default case not supported
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 struct fsal_obj_handle *obj_hdl,
				 const char *name)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* seek
 * default case not supported
 */

static fsal_status_t file_seek(struct fsal_obj_handle *obj_hdl,
				struct io_info *info)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* io advise
 * default case not supported
 */

static fsal_status_t file_io_advise(struct fsal_obj_handle *obj_hdl,
				struct io_hints *hints)
{
	hints->hints = 0;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* file_close
 * default case not supported
 */

static fsal_status_t file_close(struct fsal_obj_handle *obj_hdl)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* fallocate
 * default case not supported
 */

static fsal_status_t file_fallocate(struct fsal_obj_handle *obj_hdl,
				    struct state_t *state, uint64_t offset,
				    uint64_t length, bool allocate)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* list_ext_attrs
 * default case not supported
 */

static fsal_status_t list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				    unsigned int cookie,
				    fsal_xattrent_t *xattrs_tab,
				    unsigned int xattrs_tabsize,
				    unsigned int *p_nb_returned,
				    int *end_of_list)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* getextattr_id_by_name
 * default case not supported
 */

static fsal_status_t getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   unsigned int *pxattr_id)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* getextattr_value_by_name
 * default case not supported
 */

static fsal_status_t getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					      const char *xattr_name,
					      void *buffer_addr,
					      size_t buffer_size,
					      size_t *p_output_size)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* getextattr_value_by_id
 * default case not supported
 */

static fsal_status_t getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    void *buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* setextattr_value
 * default case not supported
 */

static fsal_status_t setextattr_value(struct fsal_obj_handle *obj_hdl,
				      const char *xattr_name,
				      void *buffer_addr, size_t buffer_size,
				      int create)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* setextattr_value_by_id
 * default case not supported
 */

static fsal_status_t setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    void *buffer_addr,
					    size_t buffer_size)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* remove_extattr_by_id
 * default case not supported
 */

static fsal_status_t remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* remove_extattr_by_name
 * default case not supported
 */

static fsal_status_t remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *xattr_name)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* handle_to_wire
 * default case server fault
 */

static fsal_status_t handle_to_wire(const struct fsal_obj_handle *obj_hdl,
				    fsal_digesttype_t output_type,
				    struct gsh_buffdesc *fh_desc)
{
	return fsalstat(ERR_FSAL_SERVERFAULT, 0);
}

/**
 * handle_cmp
 * default case compare with handle_to_key
 */
static bool handle_cmp(struct fsal_obj_handle *obj_hdl1,
		       struct fsal_obj_handle *obj_hdl2)
{
	struct gsh_buffdesc key1, key2;

	if (obj_hdl1 == NULL || obj_hdl2 == NULL)
		return false;

	if (obj_hdl1 == obj_hdl2)
		return true;

	obj_hdl1->obj_ops->handle_to_key(obj_hdl1, &key1);
	obj_hdl2->obj_ops->handle_to_key(obj_hdl2, &key2);
	if (key1.len != key2.len)
		return false;
	return !memcmp(key1.addr, key2.addr, key1.len);
}

/**
 * handle_to_key
 * default case return a safe empty key
 */

static void handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	fh_desc->addr = obj_hdl;
	fh_desc->len = 0;
}

/**
 * @brief Fail to grant a layout segment.
 *
 * @param[in]     obj_hdl  The handle of the file on which the layout is
 *                         requested.
 * @param[in]     req_ctx  Request context
 * @param[out]    loc_body An XDR stream to which the FSAL must encode
 *                         the layout specific portion of the granted
 *                         layout segment.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return NFS4ERR_LAYOUTUNAVAILABLE
 */
static nfsstat4 layoutget(struct fsal_obj_handle *obj_hdl,
			  struct req_op_context *req_ctx, XDR *loc_body,
			  const struct fsal_layoutget_arg *arg,
			  struct fsal_layoutget_res *res)
{
	return NFS4ERR_LAYOUTUNAVAILABLE;
}

/**
 * @brief Don't return a layout segment
 *
 * @param[in] obj_hdl  The object on which a segment is to be returned
 * @param[in] req_ctx  Request context
 * @param[in] lrf_body In the case of a non-synthetic return, this is
 *                     an XDR stream corresponding to the layout
 *                     type-specific argument to LAYOUTRETURN.  In
 *                     the case of a synthetic or bulk return,
 *                     this is a NULL pointer.
 * @param[in] arg      Input arguments of the function
 *
 * @return NFS4ERR_NOTSUPP
 */
static nfsstat4 layoutreturn(struct fsal_obj_handle *obj_hdl,
			     struct req_op_context *req_ctx, XDR *lrf_body,
			     const struct fsal_layoutreturn_arg *arg)
{
	return NFS4ERR_NOTSUPP;
}

/**
 * @brief Fail to commit a segment of a layout
 *
 * @param[in]     obj_hdl  The object on which to commit
 * @param[in]     req_ctx  Request context
 * @param[in]     lou_body An XDR stream containing the layout
 *                         type-specific portion of the LAYOUTCOMMIT
 *                         arguments.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 366.
 */
static nfsstat4 layoutcommit(struct fsal_obj_handle *obj_hdl,
			     struct req_op_context *req_ctx, XDR *lou_body,
			     const struct fsal_layoutcommit_arg *arg,
			     struct fsal_layoutcommit_res *res)
{
	return NFS4ERR_NOTSUPP;
}

/* open2
 * default case not supported
 */

static fsal_status_t open2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *fd,
			   fsal_openflags_t openflags,
			   enum fsal_create_mode createmode,
			   const char *name,
			   struct attrlist *attrib_set,
			   fsal_verifier_t verifier,
			   struct fsal_obj_handle **new_obj,
			   struct attrlist *attrs_out,
			   bool *caller_perm_check)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/**
 * @brief Check the exclusive create verifier for a file.
 *
 * The default behavior is to check verifier against atime and mtime.
 *
 * @param[in] obj_hdl     File to check verifier
 * @param[in] verifier    Verifier to use for exclusive create
 *
 * @retval true if verifier matches
 */

static bool check_verifier(struct fsal_obj_handle *obj_hdl,
			   fsal_verifier_t verifier)
{
	struct attrlist attrs;
	bool result;

	fsal_prepare_attrs(&attrs, ATTR_ATIME | ATTR_MTIME);

	if (FSAL_IS_ERROR(obj_hdl->obj_ops->getattrs(obj_hdl, &attrs)))
		return false;

	result = check_verifier_attrlist(&attrs, verifier);

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	return result;
}

/* status2
 * default case return openflags
 */

static fsal_openflags_t status2(struct fsal_obj_handle *obj_hdl,
				struct state_t *state)
{
	struct fsal_fd *fd = (struct fsal_fd *)(state + 1);

	return fd->openflags;
}

/* reopen2
 * default case not supported
 */

static fsal_status_t reopen2(struct fsal_obj_handle *obj_hdl,
			     struct state_t *state,
			     fsal_openflags_t openflags)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* read2
 * default case not supported
 */

static void read2(struct fsal_obj_handle *obj_hdl,
		  bool bypass,
		  fsal_async_cb done_cb,
		  struct fsal_io_arg *read_arg,
		  void *caller_arg)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP), read_arg,
		caller_arg);
}

/* write2
 * default case not supported
 */

static void write2(struct fsal_obj_handle *obj_hdl,
		   bool bypass,
		   fsal_async_cb done_cb,
		   struct fsal_io_arg *write_arg,
		   void *caller_arg)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP), write_arg,
		caller_arg);
}

/* seek2
 * default case not supported
 */

static fsal_status_t seek2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *fd,
			   struct io_info *info)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* io io_advise2
 * default case not supported
 */

static fsal_status_t io_advise2(struct fsal_obj_handle *obj_hdl,
				struct state_t *fd,
				struct io_hints *hints)
{
	hints->hints = 0;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* commit2
 * default case not supported
 */

static fsal_status_t commit2(struct fsal_obj_handle *obj_hdl,
			     off_t offset,
			     size_t len)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* lock_op2
 * default case not supported
 */

static fsal_status_t lock_op2(struct fsal_obj_handle *obj_hdl,
			      struct state_t *state,
			      void *p_owner,
			      fsal_lock_op_t lock_op,
			      fsal_lock_param_t *request_lock,
			      fsal_lock_param_t *conflicting_lock)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* lease_op2
 * default case not supported
 */

static fsal_status_t lease_op2(struct fsal_obj_handle *obj_hdl,
				  struct state_t *state,
				  void *owner,
				  fsal_deleg_t deleg)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* setattr2
 * default case not supported
 */

static fsal_status_t setattr2(struct fsal_obj_handle *obj_hdl,
			      bool bypass,
			      struct state_t *state,
			      struct attrlist *attrs)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* close2
 * default case not supported
 */

static fsal_status_t close2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *fd)
{
	LogCrit(COMPONENT_FSAL,
		"Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

/* is_referral
 * default case not a referral
 */
static bool is_referral(struct fsal_obj_handle *obj_hdl,
			struct attrlist *attrs,
			bool cache_attrs)
{
	return false;
}

/* Default fsal handle object method vector.
 * copied to allocated vector at register time
 */

struct fsal_obj_ops def_handle_ops = {
	.get_ref = handle_get_ref,
	.put_ref = handle_put_ref,
	.release = handle_release,
	.merge = handle_merge,
	.lookup = lookup,
	.readdir = read_dirents,
	.compute_readdir_cookie = compute_readdir_cookie,
	.dirent_cmp = dirent_cmp,
	.mkdir = makedir,
	.mknode = makenode,
	.symlink = makesymlink,
	.readlink = readsymlink,
	.test_access = fsal_test_access,	/* default is use common test */
	.getattrs = getattrs,
	.link = linkfile,
	.rename = renamefile,
	.unlink = file_unlink,
	.seek = file_seek,
	.io_advise = file_io_advise,
	.fallocate = file_fallocate,
	.close = file_close,
	.list_ext_attrs = list_ext_attrs,
	.getextattr_id_by_name = getextattr_id_by_name,
	.getextattr_value_by_name = getextattr_value_by_name,
	.getextattr_value_by_id = getextattr_value_by_id,
	.setextattr_value = setextattr_value,
	.setextattr_value_by_id = setextattr_value_by_id,
	.remove_extattr_by_id = remove_extattr_by_id,
	.remove_extattr_by_name = remove_extattr_by_name,
	.handle_to_wire = handle_to_wire,
	.handle_cmp = handle_cmp,
	.handle_to_key = handle_to_key,
	.layoutget = layoutget,
	.layoutreturn = layoutreturn,
	.layoutcommit = layoutcommit,
	.getxattrs = getxattrs,
	.setxattrs = setxattrs,
	.removexattrs = removexattrs,
	.listxattrs = listxattrs,
	.open2 = open2,
	.check_verifier = check_verifier,
	.status2 = status2,
	.reopen2 = reopen2,
	.read2 = read2,
	.write2 = write2,
	.seek2 = seek2,
	.io_advise2 = io_advise2,
	.commit2 = commit2,
	.lock_op2 = lock_op2,
	.lease_op2 = lease_op2,
	.setattr2 = setattr2,
	.close2 = close2,
	.is_referral = is_referral,
};

/* fsal_pnfs_ds common methods */

/**
 * @brief Clean up a pNFS Data Server
 *
 * Getting here is normal!
 *
 * @param[in] pds Handle to release
 */
static void pds_release(struct fsal_pnfs_ds *const pds)
{
	LogDebug(COMPONENT_PNFS, "Default pNFS DS release!");
	fsal_pnfs_ds_fini(pds);
	gsh_free(pds);
}

/**
 * @brief Initialize FSAL specific permissions per pNFS DS
 *
 * @param[in]  pds      FSAL pNFS DS
 * @param[in]  req      Incoming request.
 *
 * @retval NFS4_OK, NFS4ERR_ACCESS, NFS4ERR_WRONGSEC.
 */

static nfsstat4 pds_permissions(struct fsal_pnfs_ds *const pds,
				struct svc_req *req)
{
	/* FIX ME!!! Replace with a non-export dependent system.
	 * For now, reset per init_root_op_context()
	 */
	op_ctx->export_perms->set = root_op_export_set;
	op_ctx->export_perms->options = root_op_export_options;
	return NFS4_OK;
}

/**
 * @brief Try to create a FSAL data server handle
 *
 * @param[in]  pds      FSAL pNFS DS
 * @param[in]  hdl_desc Buffer from which to create the struct
 * @param[out] handle   FSAL DS handle
 *
 * @retval NFS4_OK, NFS4ERR_SERVERFAULT.
 */

static nfsstat4 pds_handle(struct fsal_pnfs_ds *const pds,
			   const struct gsh_buffdesc *const hdl_desc,
			   struct fsal_ds_handle **const handle,
			   int flags)
{
	LogCrit(COMPONENT_PNFS, "Unimplemented DS handle creation!");
	*handle = gsh_calloc(1, sizeof(struct fsal_ds_handle));

	fsal_ds_handle_init(*handle, pds);
	return NFS4_OK;
}

/**
 * @brief Initialize FSAL specific values for data server handle
 *
 * @param[in]  ops	FSAL DS handle operations vector
 */

static void pds_handle_ops(struct fsal_dsh_ops *ops)
{
	memcpy(ops, &def_dsh_ops, sizeof(struct fsal_dsh_ops));
}

struct fsal_pnfs_ds_ops def_pnfs_ds_ops = {
	.release = pds_release,
	.permissions = pds_permissions,
	.make_ds_handle = pds_handle,
	.fsal_dsh_ops = pds_handle_ops,
};

/* fsal_ds_handle common methods */

/**
 * @brief Fail to clean up a DS handle
 *
 * Getting here is bad, it means we support but have not completely
 * implemented DS handles.
 *
 * @param[in] release Handle to release
 */
static void ds_release(struct fsal_ds_handle *const ds_hdl)
{
	LogCrit(COMPONENT_PNFS, "Unimplemented DS handle release!");
	fsal_ds_handle_fini(ds_hdl);
	gsh_free(ds_hdl);
}

/**
 * @brief Fail to read from a data-server handle.
 *
 * @param[in]  ds_hdl           FSAL DS handle
 * @param[in]  req_ctx          Credentials
 * @param[in]  stateid          The stateid supplied with the READ operation,
 *                              for validation
 * @param[in]  offset           The offset at which to read
 * @param[in]  requested_length Length of read requested (and size of buffer)
 * @param[out] buffer           The buffer to which to store read data
 * @param[out] supplied_length  Length of data read
 * @param[out] eof              true on end of file
 *
 * @return NFS4ERR_NOTSUPP.
 */
static nfsstat4 ds_read(struct fsal_ds_handle *const ds_hdl,
			struct req_op_context *const req_ctx,
			const stateid4 *stateid, const offset4 offset,
			const count4 requested_length, void *const buffer,
			count4 * const supplied_length,
			bool * const end_of_file)
{
	LogCrit(COMPONENT_PNFS, "Unimplemented DS read!");
	return NFS4ERR_NOTSUPP;
}

static nfsstat4 ds_read_plus(struct fsal_ds_handle *const ds_hdl,
			struct req_op_context *const req_ctx,
			const stateid4 *stateid, const offset4 offset,
			const count4 requested_length, void *const buffer,
			const count4 supplied_length,
			bool * const end_of_file,
			struct io_info *info)
{
	LogCrit(COMPONENT_PNFS, "Unimplemented DS read_plus!");
	return NFS4ERR_NOTSUPP;
}

/**
 * @brief Fail to write to a data-server handle.
 *
 * @param[in]  ds_hdl           FSAL DS handle
 * @param[in]  req_ctx          Credentials
 * @param[in]  stateid          The stateid supplied with the READ operation,
 *                              for validation
 * @param[in]  offset           The offset at which to read
 * @param[in]  write_length     Length of write requested (and size of buffer)
 * @param[out] buffer           The buffer to which to store read data
 * @param[in]  stability wanted Stability of write
 * @param[out] written_length   Length of data written
 * @param[out] writeverf        Write verifier
 * @param[out] stability_got    Stability used for write (must be as
 *                              or more stable than request)
 *
 * @return An NFSv4.1 status code.
 */
static nfsstat4 ds_write(struct fsal_ds_handle *const ds_hdl,
			 struct req_op_context *const req_ctx,
			 const stateid4 *stateid, const offset4 offset,
			 const count4 write_length, const void *buffer,
			 const stable_how4 stability_wanted,
			 count4 * const written_length,
			 verifier4 * const writeverf,
			 stable_how4 * const stability_got)
{
	LogCrit(COMPONENT_PNFS, "Unimplemented DS write!");
	return NFS4ERR_NOTSUPP;
}

static nfsstat4 ds_write_plus(struct fsal_ds_handle *const ds_hdl,
			 struct req_op_context *const req_ctx,
			 const stateid4 *stateid, const offset4 offset,
			 const count4 write_length, const void *buffer,
			 const stable_how4 stability_wanted,
			 count4 * const written_length,
			 verifier4 * const writeverf,
			 stable_how4 * const stability_got,
			 struct io_info *info)
{
	LogCrit(COMPONENT_PNFS, "Unimplemented DS write_plus!");
	return NFS4ERR_NOTSUPP;
}

/**
 * @brief Fail to commit a byte range on a DS handle.
 *
 * @param[in]  ds_hdl    FSAL DS handle
 * @param[in]  req_ctx   Credentials
 * @param[in]  offset    Start of commit window
 * @param[in]  count     Length of commit window
 * @param[out] writeverf Write verifier
 *
 * @return An NFSv4.1 status code.
 */
static nfsstat4 ds_commit(struct fsal_ds_handle *const ds_hdl,
			  struct req_op_context *const req_ctx,
			  const offset4 offset, const count4 count,
			  verifier4 * const writeverf)
{
	LogCrit(COMPONENT_PNFS, "Unimplemented DS commit!");
	return NFS4ERR_NOTSUPP;
}

struct fsal_dsh_ops def_dsh_ops = {
	.release = ds_release,
	.read = ds_read,
	.read_plus = ds_read_plus,
	.write = ds_write,
	.write_plus = ds_write_plus,
	.commit = ds_commit
};

/** @} */
