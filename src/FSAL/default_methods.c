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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/*
 * System wide default FSAL methods
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
				   struct exportlist__ *exp_entry,
				   struct fsal_module *next_fsal,
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
				    struct netbuf *fh_desc)
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
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}

/* get_dynamic_info
 * default case is not supported
 */

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
					 fsal_dynamicfsinfo_t *infop)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}

/* fs_supports
 * default case is supports nothing
 */

static bool_t fs_supports(struct fsal_export *exp_hdl,
                          fsal_fsinfo_options_t option)
{
	return FALSE;
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

/* fs_fh_expire_type
 * default case is no type (mask)
 */

static fsal_fhexptype_t fs_fh_expire_type(struct fsal_export *exp_hdl)
{
	return 0;
}

/* fs_lease_time
 * default case is zero interval time
 */

static gsh_time_t fs_lease_time(struct fsal_export *exp_hdl)
{
        gsh_time_t lease_time = {0,0};

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
	.get_fs_dynamic_info = get_dynamic_info,
	.fs_supports = fs_supports,
	.fs_maxfilesize = fs_maxfilesize,
	.fs_maxread = fs_maxread,
	.fs_maxwrite = fs_maxwrite,
	.fs_maxlink = fs_maxlink,
	.fs_maxnamelen = fs_maxnamelen,
	.fs_maxpathlen = fs_maxpathlen,
	.fs_fh_expire_type = fs_fh_expire_type,
	.fs_lease_time = fs_lease_time,
	.fs_acl_support = fs_acl_support,
	.fs_supported_attrs = fs_supported_attrs,
	.fs_umask = fs_umask,
	.fs_xattr_access_rights = fs_xattr_access_rights,
	.check_quota = check_quota,
	.get_quota = get_quota,
	.set_quota = set_quota
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

static bool_t handle_is(struct fsal_obj_handle *obj_hdl,
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
			    const char *path,
			    struct fsal_obj_handle **handle)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* read_dirents
 * default case not supported
 */

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
				  uint32_t entry_cnt,
				  struct fsal_cookie *whence,
				  void *dir_state,
				  fsal_status_t (*cb)(
					  const char *name,
					  unsigned int dtype,
					  struct fsal_obj_handle *dir_hdl,
					  void *dir_state,
					  struct fsal_cookie *cookie),
                                  bool_t *eof)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* create
 * default case not supported
 */

static fsal_status_t create(struct fsal_obj_handle *dir_hdl,
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
                                 char *link_content,
                                 size_t *link_len,
                                 bool_t refresh)
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

/* file_truncate
 * default case not supported
 */

static fsal_status_t file_truncate(struct fsal_obj_handle *obj_hdl,
                                   uint64_t length)
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
                               uint64_t seek_descriptor,
                               size_t buffer_size,
                               void *buffer,
                               size_t *read_amount,
                               bool_t *end_of_file)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

/* file_write
 * default case not supported
 */

static fsal_status_t file_write(struct fsal_obj_handle *obj_hdl,
                                uint64_t seek_descriptor,
                                size_t buffer_size,
                                void *buffer,
                                size_t *write_amount)
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
					      size_t * p_output_size)
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
                                      struct attrlist* p_attrs)
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

fsal_status_t lru_cleanup(struct fsal_obj_handle *obj_hdl,
			      lru_actions_t requests)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* lru_cleanup
 * default case never equal
 */

static bool_t compare(struct fsal_obj_handle *obj_hdl,
                              struct fsal_obj_handle *other_hdl)
{
        return FALSE;
}

/* handle_digest
 * default case server fault
 */

static fsal_status_t handle_digest(struct fsal_obj_handle *obj_hdl,
                                   fsal_digesttype_t output_type,
                                   struct gsh_buffdesc *fh_desc)
{
        return fsalstat(ERR_FSAL_SERVERFAULT, 0);
}

/* handle_digest
 * default case return a safe empty key
 */

static void handle_to_key(struct fsal_obj_handle *obj_hdl,
                          struct gsh_buffdesc *fh_desc)
{
        fh_desc->addr = obj_hdl;
        fh_desc->len = 0;
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
	.truncate = file_truncate,
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
	.compare = compare,
	.handle_digest = handle_digest,
	.handle_to_key = handle_to_key
};
