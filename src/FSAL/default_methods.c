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
 * @defgroup FSAL File-System Abstraction Layer
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
#include "ganesha_types.h"

/** fsal module method defaults and common methods
 */

/** NOTE
 * These are the common and default methods.  One important requirement
 * is that older fsals must safely run with newer ganesha core.
 * This is observed by the following rules:
 *
 * 1. New methods are *always* appended to the ops vector in fsal_api.h
 *
 * 2. This file is updated to add the default method.
 *
 * 3. The version numbers are bumped in fsal_api.h appropriately so
 *    version detection is correct.
 */

/* Global lock for fsal list.
 * kept in fsal_manager.c
 */

extern pthread_mutex_t fsal_lock;

/* put_fsal
 * put the fsal back that we got with lookup_fsal.
 * Indicates that we are no longer interested in it (for now)
 */

static int put_fsal(struct fsal_module *fsal_hdl)
{
	int retval = EINVAL; /* too many 'puts" */

	pthread_mutex_lock(&fsal_hdl->lock);
	if(fsal_hdl->refs > 0) {
		fsal_hdl->refs--;
		retval = 0;
	}
	pthread_mutex_unlock(&fsal_hdl->lock);
	return retval;
}

/* get_name
 * return the name of the loaded fsal.
 * Must be called while holding a reference.
 * Return a pointer to the name, possibly NULL;
 * Note! do not dereference after doing a 'put'.
 */

static const char *get_name(struct fsal_module *fsal_hdl)
{
	char *name;

	pthread_mutex_lock(&fsal_hdl->lock);
	if(fsal_hdl->refs <= 0) {
		LogCrit(COMPONENT_CONFIG,
			"Called without reference!");
		name = NULL;
	} else {
		name = fsal_hdl->name;
	}
	pthread_mutex_unlock(&fsal_hdl->lock);
	return name;
}

/* fsal_get_lib_name
 * return the pathname loaded for the fsal.
 * Must be called while holding a reference.
 * Return a pointer to the library path, possibly NULL;
 * Note! do not dereference after doing a 'put'.
 */

static const char *get_lib_name(struct fsal_module *fsal_hdl)
{
	char *path;

	pthread_mutex_lock(&fsal_hdl->lock);
	if(fsal_hdl->refs <= 0) {
		LogCrit(COMPONENT_CONFIG,
			"Called without reference!");
		path = NULL;
	} else {
		path = fsal_hdl->path;
	}
	pthread_mutex_unlock(&fsal_hdl->lock);
	return path;
}

/* unload fsal
 * called while holding the last remaining reference
 * remove from list and dlclose the module
 * if references are held, return EBUSY
 * if it is a static, return EACCES
 */

static int unload_fsal(struct fsal_module *fsal_hdl)
{
	int retval = EBUSY; /* someone still has a reference */

	pthread_mutex_lock(&fsal_lock);
	pthread_mutex_lock(&fsal_hdl->lock);
	if(fsal_hdl->refs != 0 || !glist_empty(&fsal_hdl->exports))
		goto err;
	if(fsal_hdl->dl_handle == NULL) {
		retval = EACCES;  /* cannot unload static linked fsals */
		goto err;
	}
	glist_del(&fsal_hdl->fsals);
	pthread_mutex_unlock(&fsal_hdl->lock);
	pthread_mutex_destroy(&fsal_hdl->lock);
	fsal_hdl->refs = 0;

	retval = dlclose(fsal_hdl->dl_handle);
        pthread_mutex_unlock(&fsal_lock);
	return retval;

err:
	pthread_mutex_unlock(&fsal_hdl->lock);
	pthread_mutex_unlock(&fsal_lock);
	return retval;
}

/* init_config
 * default case is we have no config so return happy
 */

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
                                 config_file_t config_struct)
{
        return fsalstat(ERR_FSAL_NO_ERROR, 0) ;
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
				   const char *export_path,
				   const char *fs_options,
				   struct exportlist *exp_entry,
				   struct fsal_module *next_fsal,
                                   const struct fsal_up_vector *upops,
				   struct fsal_export **export)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}

/* Default fsal module method vector.
 * copied to allocated vector at register time
 */

struct fsal_ops def_fsal_ops = {
	.unload = unload_fsal,
	.get_name = get_name,
	.get_lib_name = get_lib_name,
	.put = put_fsal,
	.init_config = init_config,
	.dump_config = dump_config,
	.create_export = create_export
};


/* fsal_export common methods
 */

static void export_get(struct fsal_export *exp_hdl)
{
	pthread_mutex_lock(&exp_hdl->lock);
	exp_hdl->refs++;
	pthread_mutex_unlock(&exp_hdl->lock);
}

static int export_put(struct fsal_export *exp_hdl)
{
	int retval = EINVAL; /* too many 'puts" */

	pthread_mutex_lock(&exp_hdl->lock);
	if(exp_hdl->refs > 0) {
		exp_hdl->refs--;
		retval = 0;
	}
	pthread_mutex_unlock(&exp_hdl->lock);
	return retval;
}

/* export_release
 * default case is to throw a fault error.
 * creating an export is not supported so getting here is bad
 */

static fsal_status_t export_release(struct fsal_export *exp_hdl)
{
	return fsalstat(ERR_FSAL_FAULT, 0) ;
}

/* lookup_path
 * default case is not supported
 */

fsal_status_t lookup_path(struct fsal_export *exp_hdl,
			  const struct req_op_context *opctx,
                          const char *path,
			  struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
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
                                   const struct req_op_context *opctx,
                                   struct gsh_buffdesc *hdl_desc,
                                   struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}

/**
 * @brief Fail to create a FSAL data server handle from a wire handle
 *
 * @param[in]  exp_hdl  The export in which to create the handle
 * @param[out] handle   FSAL object handle
 *
 * @retval NFS4ERR_BADHANDLE.
 */

static nfsstat4
create_ds_handle(struct fsal_export *const exp_hdl,
                 const struct gsh_buffdesc *const hdl_desc,
                 struct fsal_ds_handle **const handle)
{
        return NFS4ERR_BADHANDLE;
}

/* get_dynamic_info
 * default case is not supported
 */

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      const struct req_op_context *opctx,
                                      fsal_dynamicfsinfo_t *infop)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
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
        struct timespec lease_time = {0,0};

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
				 const char * filepath,
				 int quota_type,
				 struct req_op_context *req_ctx)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0) ;
}

/* get_quota
 * default case not supported
 */

static fsal_status_t get_quota(struct fsal_export *exp_hdl,
			       const char * filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t *pquota)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}

/* set_quota
 * default case not supported
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t * pquota,
			       fsal_quota_t * presquota)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}

/**
 * @brief Be uninformative about a device
 */

static nfsstat4
getdeviceinfo(struct fsal_export *exp_hdl,
               XDR *da_addr_body,
               const layouttype4 type,
               const struct pnfs_deviceid *deviceid)
{
        return NFS4ERR_NOTSUPP;
}

/**
 * @brief Be uninformative about all devices
 */

static nfsstat4
getdevicelist(struct fsal_export *exp_hdl,
              layouttype4 type,
              void *opaque,
              bool (*cb)(void *opaque,
                         const uint64_t id),
              struct fsal_getdevicelist_res *res)
{
        return NFS4ERR_NOTSUPP;
}

/**
 * @brief Support no layout types
 */

static void
fs_layouttypes(struct fsal_export *exp_hdl,
               size_t *count,
               const layouttype4 **types)
{
        *count = 0;
        *types = NULL;
}

/**
 * @brief Read no bytes through layouts
 */

static uint32_t
fs_layout_blocksize(struct fsal_export *exp_hdl)
{
        return 0;
}

/**
 * @brief No segments
 */

static uint32_t
fs_maximum_segments(struct fsal_export *exp_hdl)
{
        return 0;
}

/**
 * @brief No loc_body
 */

static size_t
fs_loc_body_size(struct fsal_export *exp_hdl)
{
        return 0;
}

/**
 * No da_addr.
 */

static size_t
fs_da_addr_size(struct fsal_export *exp_hdl)
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
static void
global_verifier(struct gsh_buffdesc *verf_desc)
{
        memcpy(verf_desc->addr, &NFS4_write_verifier, verf_desc->len);
};

/* Default fsal export method vector.
 * copied to allocated vector at register time
 */


struct export_ops def_export_ops = {
        .get = export_get,
        .put = export_put,
        .release = export_release,
        .lookup_path = lookup_path,
        .lookup_junction = lookup_junction,
        .extract_handle = extract_handle,
        .create_handle = create_handle,
        .create_ds_handle = create_ds_handle,
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
        .getdeviceinfo = getdeviceinfo,
        .getdevicelist = getdevicelist,
        .fs_layouttypes = fs_layouttypes,
        .fs_layout_blocksize = fs_layout_blocksize,
        .fs_maximum_segments = fs_maximum_segments,
        .fs_loc_body_size = fs_loc_body_size,
        .fs_da_addr_size = fs_da_addr_size,
        .get_write_verifier = global_verifier
};



/* fsal_obj_handle common methods
 */

static void handle_get(struct fsal_obj_handle *obj_hdl)
{
	pthread_mutex_lock(&obj_hdl->lock);
	obj_hdl->refs++;
	pthread_mutex_unlock(&obj_hdl->lock);
}

static int handle_put(struct fsal_obj_handle *obj_hdl)
{
	int retval = EINVAL; /* too many 'puts" */

	pthread_mutex_lock(&obj_hdl->lock);
	if(obj_hdl->refs > 0) {
		obj_hdl->refs--;
		retval = 0;
	}
	pthread_mutex_unlock(&obj_hdl->lock);
	return retval;
}

/* handle_is
 * test the type of this handle
 */

static bool handle_is(struct fsal_obj_handle *obj_hdl,
                        object_file_type_t type)
{
        return obj_hdl->type == type;
}

/* handle_release
 * default case is to throw a fault error.
 * creating an handle is not supported so getting here is bad
 */

static fsal_status_t handle_release(struct fsal_obj_handle *obj_hdl)
{
	return fsalstat(ERR_FSAL_FAULT, 0) ;
}

/* lookup
 * default case not supported
 */

static fsal_status_t lookup(struct fsal_obj_handle *parent,
			    const struct req_op_context *opctx,
                            const char *path,
			    struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* read_dirents
 * default case not supported
 */

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  const struct req_op_context *opctx,
				  fsal_cookie_t *whence,
				  void *dir_state,
				  fsal_readdir_cb cb,
                                  bool *eof)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* create
 * default case not supported
 */

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
                            const struct req_op_context *opctx,
                            const char *name,
                            struct attrlist *attrib,
                            struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* makedir
 * default case not supported
 */

static fsal_status_t makedir(struct fsal_obj_handle *dir_hdl,
                             const struct req_op_context *opctx,
                             const char *name,
			     struct attrlist *attrib,
			     struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* makenode
 * default case not supported
 */

static fsal_status_t makenode(struct fsal_obj_handle *dir_hdl,
                              const struct req_op_context *opctx,
                              const char *name,
                              object_file_type_t nodetype,
                              fsal_dev_t *dev,
                              struct attrlist *attrib,
                              struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* makesymlink
 * default case not supported
 */

static fsal_status_t makesymlink(struct fsal_obj_handle *dir_hdl,
                                 const struct req_op_context *opctx,
                                 const char *name,
                                 const char *link_path,
                                 struct attrlist *attrib,
                                 struct fsal_obj_handle **handle)
{
        return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* readsymlink
 * default case not supported
 */

static fsal_status_t readsymlink(struct fsal_obj_handle *obj_hdl,
                                 const struct req_op_context *opctx,
                                 struct gsh_buffdesc *link_content,
                                 bool refresh)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* getattrs
 * default case not supported
 */

static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* setattrs
 * default case not supported
 */

static fsal_status_t setattrs(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx,
                              struct attrlist *attrs)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* linkfile
 * default case not supported
 */

static fsal_status_t linkfile(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx,
                              struct fsal_obj_handle *destdir_hdl,
                              const char *name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* renamefile
 * default case not supported
 */

static fsal_status_t renamefile(struct fsal_obj_handle *olddir_hdl,
                                const struct req_op_context *opctx,
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
                                 const struct req_op_context *opctx,
                                 const char *name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}


/* file_open
 * default case not supported
 */

static fsal_status_t file_open(struct fsal_obj_handle *obj_hdl,
			       const struct req_op_context *opctx,
			       fsal_openflags_t openflags)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_status
 * default case file always closed
 */

static fsal_openflags_t file_status(struct fsal_obj_handle *obj_hdl)
{
	return  FSAL_O_CLOSED;
}

/* file_read
 * default case not supported
 */

static fsal_status_t file_read(struct fsal_obj_handle *obj_hdl,
                               const struct req_op_context *opctx,
                               uint64_t seek_descriptor,
                               size_t buffer_size,
                               void *buffer,
                               size_t *read_amount,
                               bool *end_of_file)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_write
 * default case not supported
 */

static fsal_status_t file_write(struct fsal_obj_handle *obj_hdl,
                                const struct req_op_context *opctx,
                                uint64_t seek_descriptor,
                                size_t buffer_size,
                                void *buffer,
                                size_t *write_amount,
                                bool *fsal_stable)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* commit
 * default case not supported
 */

static fsal_status_t commit(struct fsal_obj_handle *obj_hdl, /* sync */
			    off_t offset,
			    size_t len)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* lock_op
 * default case not supported
 */

static fsal_status_t lock_op(struct fsal_obj_handle *obj_hdl,
			     const struct req_op_context *opctx,
			     void * p_owner,
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
			      fsal_share_param_t  request_share)
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
                                    const struct req_op_context *opctx,
				    unsigned int cookie,
				    fsal_xattrent_t * xattrs_tab,
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
                                           const struct req_op_context *opctx,
					   const char *xattr_name,
					   unsigned int *pxattr_id)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* getextattr_value_by_name
 * default case not supported
 */

static fsal_status_t getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
                                              const struct req_op_context *opctx,
					      const char *xattr_name,
					      caddr_t buffer_addr,
					      size_t buffer_size,
					      size_t * p_output_size)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* getextattr_value_by_id
 * default case not supported
 */

static fsal_status_t getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
                                            const struct req_op_context *opctx,
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
                                      const struct req_op_context *opctx,
				      const char *xattr_name,
				      caddr_t buffer_addr,
				      size_t buffer_size,
				      int create)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* setextattr_value_by_id
 * default case not supported
 */

static fsal_status_t setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
                                            const struct req_op_context *opctx,
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
                                      const struct req_op_context *opctx,
                                      unsigned int xattr_id,
                                      struct attrlist* p_attrs)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* remove_extattr_by_id
 * default case not supported
 */

static fsal_status_t remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
                                          const struct req_op_context *opctx,
					  unsigned int xattr_id)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* remove_extattr_by_name
 * default case not supported
 */

static fsal_status_t remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
                                            const struct req_op_context *opctx,
					    const char *xattr_name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* lru_cleanup
 * default case always be happy
 */

fsal_status_t lru_cleanup(struct fsal_obj_handle *obj_hdl,
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
static nfsstat4
layoutget(struct fsal_obj_handle *obj_hdl,
          struct req_op_context *req_ctx,
          XDR *loc_body,
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
static nfsstat4
layoutreturn(struct fsal_obj_handle *obj_hdl,
             struct req_op_context *req_ctx,
             XDR *lrf_body,
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
static nfsstat4
layoutcommit(struct fsal_obj_handle *obj_hdl,
             struct req_op_context *req_ctx,
             XDR *lou_body,
             const struct fsal_layoutcommit_arg *arg,
             struct fsal_layoutcommit_res *res)
{
        return NFS4ERR_NOTSUPP;
}


/* Default fsal handle object method vector.
 * copied to allocated vector at register time
 */

struct fsal_obj_ops def_handle_ops = {
        .get = handle_get,
        .put = handle_put,
        .release = handle_release,
        .lookup = lookup,
        .readdir = read_dirents,
        .create = create,
        .mkdir = makedir,
        .mknode = makenode,
        .symlink = makesymlink,
        .readlink = readsymlink,
        .test_access = fsal_test_access, /* default is use common test */
        .getattrs = getattrs,
        .setattrs = setattrs,
        .link = linkfile,
        .rename = renamefile,
        .unlink = file_unlink,
        .open = file_open,
        .status = file_status,
        .read = file_read,
        .write = file_write,
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

/* fsal_ds_handle common methods */

/**
 * @brief Get a reference on a handle
 *
 * This function increments the reference count on a handle.  It
 * should not be overridden.
 *
 * @param[in] obj_hdl The handle to reference
 */

static void
ds_get(struct fsal_ds_handle *const ds_hdl)
{
        pthread_mutex_lock(&ds_hdl->lock);
        if (ds_hdl->refs > 0) {
                ds_hdl->refs++;
        }
        pthread_mutex_unlock(&ds_hdl->lock);
}

/**
 * @brief Release a reference on a handle
 *
 * This function releases a reference to a handle.  Once a caller's
 * reference is released they should make no attempt to access the
 * handle or even dereference a pointer to it.  This function should
 * not be overridden.
 *
 * @param[in] obj_hdl The handle to relinquish
 *
 * @return NFS status codes.
 */
static nfsstat4
ds_put(struct fsal_ds_handle *const ds_hdl)
{
        int retval = EINVAL; /* too many 'puts" */

        pthread_mutex_lock(&ds_hdl->lock);
        if (ds_hdl->refs > 0) {
                ds_hdl->refs--;
                retval = 0;
        }
        pthread_mutex_unlock(&ds_hdl->lock);
        if (ds_hdl->refs == 0) {
                return ds_hdl->ops->release(ds_hdl);
        }

        return retval;
}

/**
 * @brief Fail to clean up a filehandle
 *
 * Getting here is bad, it means we support but have not completely
 * impelmented DS handles.
 *
 * @param[in] release Handle to release
 *
 * @return NFSv4.1 status codes.
 */
static nfsstat4
ds_release(struct fsal_ds_handle *const ds_hdl)
{
        LogCrit(COMPONENT_PNFS,
                "Unimplemented DS release!");
        return NFS4ERR_SERVERFAULT;
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
static nfsstat4
ds_read(struct fsal_ds_handle *const ds_hdl,
        struct req_op_context *const req_ctx,
        const stateid4 *stateid,
        const offset4 offset,
        const count4 requested_length,
        void *const buffer,
        count4 *const supplied_length,
        bool *const end_of_file)
{
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
static nfsstat4
ds_write(struct fsal_ds_handle *const ds_hdl,
         struct req_op_context *const req_ctx,
         const stateid4 *stateid,
         const offset4 offset,
         const count4 write_length,
         const void *buffer,
         const stable_how4 stability_wanted,
         count4 *const written_length,
         verifier4 *const writeverf,
         stable_how4 *const stability_got)
{
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
static nfsstat4
ds_commit(struct fsal_ds_handle *const ds_hdl,
          struct req_op_context *const req_ctx,
          const offset4 offset,
          const count4 count,
          verifier4 *const writeverf)
{
        return NFS4ERR_NOTSUPP;
}

struct fsal_ds_ops def_ds_ops = {
        .get = ds_get,
        .put = ds_put,
        .release = ds_release,
        .read = ds_read,
        .write = ds_write,
        .commit = ds_commit
};
/** @} */
