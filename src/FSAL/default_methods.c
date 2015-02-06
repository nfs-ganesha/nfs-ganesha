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

	pthread_mutex_lock(&fsal_lock);

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
	pthread_rwlock_destroy(&fsal_hdl->lock);

	retval = dlclose(fsal_hdl->dl_handle);
	pthread_mutex_unlock(&fsal_lock);
	return retval;

 err:
	PTHREAD_RWLOCK_unlock(&fsal_hdl->lock);
	pthread_mutex_unlock(&fsal_lock);
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
	return;
}

/* create_export
 * default is we cannot create an export
 */

static fsal_status_t create_export(struct fsal_module *fsal_hdl,
				   void *parse_node,
				   struct config_error_type *err_type,
				   const struct fsal_up_vector *up_ops)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/**
 * @brief Default emergency cleanup method
 *
 * Do nothing.
 */

static void emergency_cleanup(void)
{
	return;
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
	if (*handle == NULL) {
		*handle = pnfs_ds_alloc();
		if (*handle == NULL)
			return fsalstat(ERR_FSAL_NOMEM, ENOMEM);
	}

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
};

/* export_release
 * default case is to throw a fault error.
 * creating an export is not supported so getting here is bad
 */

static void export_release(struct fsal_export *exp_hdl)
{
	return;
}

/* lookup_path
 * default case is not supported
 */

fsal_status_t lookup_path(struct fsal_export *exp_hdl,
			  const char *path,
			  struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

static fsal_status_t lookup_junction(struct fsal_export *exp_hdl,
				     struct fsal_obj_handle *junction,
				     struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* extract_handle
 * default case is not supported
 */

static fsal_status_t extract_handle(struct fsal_export *exp_hdl,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* create_handle
 * default case is not supported
 */

static fsal_status_t create_handle(struct fsal_export *exp_hdl,
				   struct gsh_buffdesc *hdl_desc,
				   struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* get_dynamic_info
 * default case is not supported
 */

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* fs_supports
 * default case is supports nothing
 */

static bool fs_supports(struct fsal_export *exp_hdl,
			fsal_fsinfo_options_t option)
{
	return false;
}

/* fs_maxfilesize
 * default case is zero size
 */

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	return 0;
}

/* fs_maxread
 * default case is zero length
 */

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	return 0;
}

/* fs_maxwrite
 * default case is zero length
 */

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	return 0;
}

/* fs_maxlink
 * default case is zero links
 */

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	return 0;
}

/* fs_maxnamelen
 * default case is zero length
 */

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	return 0;
}

/* fs_maxpathlen
 * default case is zero length
 */

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	return 0;
}

/* fs_lease_time
 * default case is zero interval time
 */

static struct timespec fs_lease_time(struct fsal_export *exp_hdl)
{
	struct timespec lease_time = { 0, 0 };

	return lease_time;
}

/* fs_acl_support
 * default case is none, neither deny or allow
 */

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	return 0;
}

/* fs_supported_attrs
 * default case is none
 */

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	return 0;
}

/* fs_umask
 * default case is no access
 */

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	return 0000;
}

/* fs_xattr_access_rights
 * default case is no access
 */

static uint32_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	return 0000;
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
			       fsal_quota_t *pquota)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* set_quota
 * default case not supported
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath, int quota_type,
			       fsal_quota_t *pquota, fsal_quota_t *presquota)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
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
 * @param[in/out] verf_desc Address and length of verifier
 *
 * @return No errors
 */
static void global_verifier(struct gsh_buffdesc *verf_desc)
{
	memcpy(verf_desc->addr, &NFS4_write_verifier, verf_desc->len);
};

/* Default fsal export method vector.
 * copied to allocated vector at register time
 */

struct export_ops def_export_ops = {
	.release = export_release,
	.lookup_path = lookup_path,
	.lookup_junction = lookup_junction,
	.extract_handle = extract_handle,
	.create_handle = create_handle,
	.get_fs_dynamic_info = get_dynamic_info,
	.fs_supports = fs_supports,
	.fs_maxfilesize = fs_maxfilesize,
	.fs_maxread = fs_maxread,
	.fs_maxwrite = fs_maxwrite,
	.fs_maxlink = fs_maxlink,
	.fs_maxnamelen = fs_maxnamelen,
	.fs_maxpathlen = fs_maxpathlen,
	.fs_lease_time = fs_lease_time,
	.fs_acl_support = fs_acl_support,
	.fs_supported_attrs = fs_supported_attrs,
	.fs_umask = fs_umask,
	.fs_xattr_access_rights = fs_xattr_access_rights,
	.check_quota = check_quota,
	.get_quota = get_quota,
	.set_quota = set_quota,
	.getdevicelist = getdevicelist,
	.fs_layouttypes = fs_layouttypes,
	.fs_layout_blocksize = fs_layout_blocksize,
	.fs_maximum_segments = fs_maximum_segments,
	.fs_loc_body_size = fs_loc_body_size,
	.get_write_verifier = global_verifier
};

/* fsal_obj_handle common methods
 */

/* handle_is
 * test the type of this handle
 */

static bool handle_is(struct fsal_obj_handle *obj_hdl, object_file_type_t type)
{
	return obj_hdl->type == type;
}

/* handle_release
 * default case is to throw a fault error.
 * creating an handle is not supported so getting here is bad
 */

static void handle_release(struct fsal_obj_handle *obj_hdl)
{
	return;
}

/* lookup
 * default case not supported
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const char *path, struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* read_dirents
 * default case not supported
 */

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence, void *dir_state,
				  fsal_readdir_cb cb, bool *eof)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* create
 * default case not supported
 */

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
			    const char *name, struct attrlist *attrib,
			    struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* makedir
 * default case not supported
 */

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
			     const char *name, struct attrlist *attrib,
			     struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* makenode
 * default case not supported
 */

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
			      const char *name, object_file_type_t nodetype,
			      fsal_dev_t *dev, struct attrlist *attrib,
			      struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* makesymlink
 * default case not supported
 */

static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
				 const char *name, const char *link_path,
				 struct attrlist *attrib,
				 struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* readsymlink
 * default case not supported
 */

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
				 struct gsh_buffdesc *link_content,
				 bool refresh)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* getattrs
 * default case not supported
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* setattrs
 * default case not supported
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
			      struct attrlist *attrs)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* linkfile
 * default case not supported
 */

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
			      struct fsal_obj_handle *destdir_hdl,
			      const char *name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* renamefile
 * default case not supported
 */

static fsal_status_t renamefile(struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_unlink
 * default case not supported
 */

static fsal_status_t file_unlink(struct fsal_obj_handle *dir_hdl,
				 const char *name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_open
 * default case not supported
 */

static fsal_status_t file_open(struct fsal_obj_handle *obj_hdl,
			       fsal_openflags_t openflags)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_reopen
 * default case not supported
 */

static fsal_status_t file_reopen(struct fsal_obj_handle *obj_hdl,
				 fsal_openflags_t openflags)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* fs_locations
 * default case not supported
 */

static fsal_status_t fs_locations(struct fsal_obj_handle *obj_hdl,
				 struct fs_locations4 *fs_locs)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_status
 * default case file always closed
 */

static fsal_openflags_t file_status(struct fsal_obj_handle *obj_hdl)
{
	return FSAL_O_CLOSED;
}

/* file_read
 * default case not supported
 */

static fsal_status_t file_read(struct fsal_obj_handle *obj_hdl,
			       uint64_t seek_descriptor, size_t buffer_size,
			       void *buffer, size_t *read_amount,
			       bool *end_of_file)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_read_plus
 * default case not supported
 */

static fsal_status_t file_read_plus(struct fsal_obj_handle *obj_hdl,
			       uint64_t seek_descriptor, size_t buffer_size,
			       void *buffer, size_t *read_amount,
			       bool *end_of_file, struct io_info *info)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_write
 * default case not supported
 */

static fsal_status_t file_write(struct fsal_obj_handle *obj_hdl,
				uint64_t seek_descriptor, size_t buffer_size,
				void *buffer, size_t *write_amount,
				bool *fsal_stable)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_write_plus
 * default case not supported
 */

static fsal_status_t file_write_plus(struct fsal_obj_handle *obj_hdl,
				uint64_t seek_descriptor, size_t buffer_size,
				void *buffer, size_t *write_amount,
				bool *fsal_stable, struct io_info *info)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* seek
 * default case not supported
 */

static fsal_status_t file_seek(struct fsal_obj_handle *obj_hdl,
				struct io_info *info)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
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

/* commit
 * default case not supported
 */

static fsal_status_t commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			    off_t offset, size_t len)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* lock_op
 * default case not supported
 */

static fsal_status_t lock_op(struct fsal_obj_handle *obj_hdl,
			     void *p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *request_lock,
			     fsal_lock_param_t *conflicting_lock)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* share_op
 * default case not supported
 */

static fsal_status_t share_op(struct fsal_obj_handle *obj_hdl,
			      void *p_owner,
			      fsal_share_param_t request_share)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_close
 * default case not supported
 */

static fsal_status_t file_close(struct fsal_obj_handle *obj_hdl)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
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
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* getextattr_id_by_name
 * default case not supported
 */

static fsal_status_t getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   unsigned int *pxattr_id)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* getextattr_value_by_name
 * default case not supported
 */

static fsal_status_t getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					      const char *xattr_name,
					      caddr_t buffer_addr,
					      size_t buffer_size,
					      size_t *p_output_size)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* getextattr_value_by_id
 * default case not supported
 */

static fsal_status_t getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* setextattr_value
 * default case not supported
 */

static fsal_status_t setextattr_value(struct fsal_obj_handle *obj_hdl,
				      const char *xattr_name,
				      caddr_t buffer_addr, size_t buffer_size,
				      int create)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* setextattr_value_by_id
 * default case not supported
 */

static fsal_status_t setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* getextattr_attrs
 * default case not supported
 */

static fsal_status_t getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				      unsigned int xattr_id,
				      struct attrlist *p_attrs)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* remove_extattr_by_id
 * default case not supported
 */

static fsal_status_t remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* remove_extattr_by_name
 * default case not supported
 */

static fsal_status_t remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *xattr_name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* lru_cleanup
 * default case always be happy
 */

static fsal_status_t lru_cleanup(struct fsal_obj_handle *obj_hdl,
				 lru_actions_t requests)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* handle_digest
 * default case server fault
 */

static fsal_status_t handle_digest(const struct fsal_obj_handle *obj_hdl,
				   fsal_digesttype_t output_type,
				   struct gsh_buffdesc *fh_desc)
{
	return fsalstat(ERR_FSAL_SERVERFAULT, 0);
}

/**
 * handle_digest
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

/* Default fsal handle object method vector.
 * copied to allocated vector at register time
 */

struct fsal_obj_ops def_handle_ops = {
	.release = handle_release,
	.lookup = lookup,
	.readdir = read_dirents,
	.create = create,
	.mkdir = makedir,
	.mknode = makenode,
	.symlink = makesymlink,
	.readlink = readsymlink,
	.test_access = fsal_test_access,	/* default is use common test */
	.getattrs = getattrs,
	.setattrs = setattrs,
	.link = linkfile,
	.rename = renamefile,
	.unlink = file_unlink,
	.open = file_open,
	.reopen = file_reopen,
	.fs_locations = fs_locations,
	.status = file_status,
	.read = file_read,
	.read_plus = file_read_plus,
	.write = file_write,
	.write_plus = file_write_plus,
	.seek = file_seek,
	.io_advise = file_io_advise,
	.commit = commit,
	.lock_op = lock_op,
	.share_op = share_op,
	.close = file_close,
	.list_ext_attrs = list_ext_attrs,
	.getextattr_id_by_name = getextattr_id_by_name,
	.getextattr_value_by_name = getextattr_value_by_name,
	.getextattr_value_by_id = getextattr_value_by_id,
	.setextattr_value = setextattr_value,
	.setextattr_value_by_id = setextattr_value_by_id,
	.getextattr_attrs = getextattr_attrs,
	.remove_extattr_by_id = remove_extattr_by_id,
	.remove_extattr_by_name = remove_extattr_by_name,
	.handle_is = handle_is,
	.lru_cleanup = lru_cleanup,
	.handle_digest = handle_digest,
	.handle_to_key = handle_to_key,
	.layoutget = layoutget,
	.layoutreturn = layoutreturn,
	.layoutcommit = layoutcommit
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
			   struct fsal_ds_handle **const handle)
{
	LogCrit(COMPONENT_PNFS, "Unimplemented DS handle creation!");
	*handle = gsh_calloc(sizeof(struct fsal_ds_handle), 1);
	if (*handle == NULL)
		return NFS4ERR_SERVERFAULT;

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
