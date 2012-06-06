/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Max Matveev, 2012
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <pthread.h>
#include "nlm_list.h"
#include "FSAL/fsal_commonlib.h"
#include "pxy_fsal_methods.h"


/* Proxy handle methods */

static fsal_status_t
pxy_lookup(struct fsal_obj_handle *parent,
	   const char *path,
	   struct fsal_obj_handle **handle)
{
        ReturnCode(ERR_FSAL_NOENT, ENOENT);
}

static fsal_status_t
pxy_create(struct fsal_obj_handle *dir_hdl,
	   fsal_name_t *name,
	   fsal_attrib_list_t *attrib,
	   struct fsal_obj_handle **handle)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t 
pxy_mkdir(struct fsal_obj_handle *dir_hdl,
	  fsal_name_t *name,
	  fsal_attrib_list_t *attrib,
	  struct fsal_obj_handle **handle)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_mknod(struct fsal_obj_handle *dir_hdl,
	  fsal_name_t *name,
	  object_file_type_t nodetype, 
	  fsal_dev_t *dev, 
	  fsal_attrib_list_t *attrib,
          struct fsal_obj_handle **handle)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_symlink(struct fsal_obj_handle *dir_hdl,
	    fsal_name_t *name,
	    fsal_path_t *link_path,
	    fsal_attrib_list_t *attrib,
	    struct fsal_obj_handle **handle)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_readlink(struct fsal_obj_handle *obj_hdl,
	     char *link_content,
	     uint32_t *link_len,
	     fsal_boolean_t refresh)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_link(struct fsal_obj_handle *obj_hdl,
	 struct fsal_obj_handle *destdir_hdl,
	 fsal_name_t *name)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

typedef fsal_status_t (*fsal_readdir_cb)(const char *name,
					 unsigned int dtype,
					 struct fsal_obj_handle *dir_hdl,
					 void *dir_state,
					 struct fsal_cookie *cookie);

static fsal_status_t
pxy_readdir(struct fsal_obj_handle *dir_hdl,
	    uint32_t entry_cnt,
	    struct fsal_cookie *whence,
            void *dir_state,
            fsal_readdir_cb cb,
            fsal_boolean_t *eof)
{
        ReturnCode(ERR_FSAL_NOTDIR, ENOTDIR);
}

static fsal_status_t
pxy_rename(struct fsal_obj_handle *olddir_hdl,
	   fsal_name_t *old_name,
	   struct fsal_obj_handle *newdir_hdl,
	   fsal_name_t *new_name)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_getattrs(struct fsal_obj_handle *obj_hdl,
	     fsal_attrib_list_t *obj_attr)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_setattrs(struct fsal_obj_handle *obj_hdl,
	     fsal_attrib_list_t *attrs)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_boolean_t 
pxy_handle_is(struct fsal_obj_handle *obj_hdl,
	      object_file_type_t type)
{
	return 0;
}

static fsal_boolean_t 
pxy_compare_hdl(struct fsal_obj_handle *obj_hdl,
	        struct fsal_obj_handle *other_hdl)
{
	return 0;
}

static fsal_status_t
pxy_truncate(struct fsal_obj_handle *obj_hdl,
	     fsal_size_t length)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_unlink(struct fsal_obj_handle *dir_hdl,
	   fsal_name_t *name)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t 
pxy_handle_digest(struct fsal_obj_handle *obj_hdl,
		  fsal_digesttype_t output_type,
		  struct fsal_handle_desc *fh_desc)
{
	/* sanity checks */
	if( !fh_desc || !fh_desc->start)
		ReturnCode(ERR_FSAL_FAULT, 0);

	switch(output_type) {
	case FSAL_DIGEST_NFSV2:
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		break;
	case FSAL_DIGEST_FILEID2:
		break;
	case FSAL_DIGEST_FILEID3:
		break;
	case FSAL_DIGEST_FILEID4:
		break;
	default:
		ReturnCode(ERR_FSAL_SERVERFAULT, 0);
	}
	ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static void
pxy_handle_to_key(struct fsal_obj_handle *obj_hdl,
		  struct fsal_handle_desc *fh_desc)
{
	fh_desc->start = (caddr_t)obj_hdl;
	fh_desc->len =  0;
}

static fsal_status_t
pxy_hdl_release(struct fsal_obj_handle *obj_hdl)
{
	ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

struct fsal_obj_ops pxy_obj_ops = {
	.get = fsal_handle_get,
	.put = fsal_handle_put,
	.release = pxy_hdl_release,
	.lookup = pxy_lookup,
	.readdir = pxy_readdir,
	.create = pxy_create,
	.mkdir = pxy_mkdir,
	.mknode = pxy_mknod,
	.symlink = pxy_symlink,
	.readlink = pxy_readlink,
	.test_access = fsal_test_access,
	.getattrs = pxy_getattrs,
	.setattrs = pxy_setattrs,
	.link = pxy_link,
	.rename = pxy_rename,
	.unlink = pxy_unlink,
	.truncate = pxy_truncate,
	.open = pxy_open,
	.read = pxy_read,
	.write = pxy_write,
	.commit = pxy_commit,
	.lock_op = pxy_lock_op,
	.share_op = pxy_share_op,
	.close = pxy_close,
	.rcp = pxy_rcp,
	.getextattrs = pxy_getextattrs,
	.list_ext_attrs = pxy_list_ext_attrs,
	.getextattr_id_by_name = pxy_getextattr_id_by_name,
	.getextattr_value_by_name = pxy_getextattr_value_by_name,
	.getextattr_value_by_id = pxy_getextattr_value_by_id,
	.setextattr_value = pxy_setextattr_value,
	.setextattr_value_by_id = pxy_setextattr_value_by_id,
	.getextattr_attrs = pxy_getextattr_attrs,
	.remove_extattr_by_id = pxy_remove_extattr_by_id,
	.remove_extattr_by_name = pxy_remove_extattr_by_name,
	.handle_is = pxy_handle_is,
	.lru_cleanup = pxy_lru_cleanup,
	.compare = pxy_compare_hdl,
	.handle_digest = pxy_handle_digest,
	.handle_to_key = pxy_handle_to_key
};

/* export methods that create object handles
 */

fsal_status_t
pxy_lookup_path(struct fsal_export *exp_hdl,
		const char *path,
		struct fsal_obj_handle **handle)
{
        ReturnCode(ERR_FSAL_NOENT, ENOENT);
}

/* create_handle
 * Does what original FSAL_ExpandHandle did (sort of)
 * returns a ref counted handle to be later used in cache_inode etc.
 * NOTE! you must release this thing when done with it!
 */

fsal_status_t
pxy_create_handle(struct fsal_export *exp_hdl,
		  struct fsal_handle_desc *hdl_desc,
		  struct fsal_obj_handle **handle)
{
        
        ReturnCode(ERR_FSAL_NOENT, ENOENT);
}

