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
 * VFS FSAL export object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <os/mntent.h>
#include <os/quota.h>
#include <dlfcn.h>
#include "nlm_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "fsal_handle_syscalls.h"
#include "nullfs_methods.h"


/* helpers to/from other VFS objects
 */

struct fsal_staticfsinfo_t *nullfs_staticinfo(struct fsal_module *hdl);
extern struct next_ops next_ops ;

/* export object methods
 */

static fsal_status_t release(struct fsal_export *exp_hdl)
{
	struct nullfs_fsal_export *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

        /* Question : should I release next_fsal or not ? */
	next_ops.exp_ops->release( exp_hdl ) ;
	
        myself = container_of(exp_hdl, struct nullfs_fsal_export, export);


	pthread_mutex_lock(&exp_hdl->lock);
	if(exp_hdl->refs > 0 || !glist_empty(&exp_hdl->handles)) {
		LogMajor(COMPONENT_FSAL,
			 "VFS release: export (0x%p)busy",
			 exp_hdl);
		fsal_error = posix2fsal_error(EBUSY);
		retval = EBUSY;
		goto errout;
	}
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);
	pthread_mutex_unlock(&exp_hdl->lock);

	pthread_mutex_destroy(&exp_hdl->lock);
	gsh_free(myself);  /* elvis has left the building */
	return fsalstat(fsal_error, retval);

errout:
	pthread_mutex_unlock(&exp_hdl->lock);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
                                      const struct req_op_context *opctx,
			              fsal_dynamicfsinfo_t *infop)
{
	return next_ops.exp_ops->get_fs_dynamic_info( exp_hdl, opctx, infop ) ;
}

static bool fs_supports(struct fsal_export *exp_hdl,
                          fsal_fsinfo_options_t option)
{
	return next_ops.exp_ops->fs_supports( exp_hdl, option ) ;
}

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_maxfilesize( exp_hdl ) ;
}

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_maxread( exp_hdl ) ;
}

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_maxwrite( exp_hdl ) ;
}

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_maxlink( exp_hdl ) ;
}

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_maxnamelen( exp_hdl ) ;
}

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_maxpathlen( exp_hdl ) ;
}

static struct timespec fs_lease_time(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_lease_time( exp_hdl ) ;
}

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_acl_support( exp_hdl ) ;
}

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_supported_attrs( exp_hdl ) ;
}

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_umask( exp_hdl ) ;
}

static uint32_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	return next_ops.exp_ops->fs_xattr_access_rights( exp_hdl ) ;
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
			       const char * filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t *pquota)
{
	return next_ops.exp_ops->get_quota( exp_hdl, filepath,quota_type,
                                            req_ctx, pquota  ) ;  
}

/* set_quota
 * same lower mount restriction applies
 */

static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t * pquota,
			       fsal_quota_t * presquota)
{
	return next_ops.exp_ops->set_quota( exp_hdl, filepath,quota_type,
                                            req_ctx, pquota, presquota ) ;  
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
	return next_ops.exp_ops->extract_handle( exp_hdl, in_type, fh_desc ) ;
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

void nullfs_handle_ops_init(struct fsal_obj_ops *ops);


/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

extern struct fsal_up_vector fsal_up_top;

fsal_status_t nullfs_create_export(struct fsal_module *fsal_hdl,
				   const char *export_path,
				   const char *fs_specific,
				   struct exportlist *exp_entry,
				   struct fsal_module *next_fsal,
                                   struct fsal_up_vector *up_ops,
				   struct fsal_export **export)
{
        fsal_status_t expres ;
        struct fsal_module *fsal_stack;

       /* We use the parameter passed as a string in fs_specific to know which FSAL is to be loaded */
       fsal_stack = lookup_fsal( fs_specific ) ;
       if( fsal_stack == NULL )
       {
	   LogMajor(COMPONENT_FSAL,
	            "nullfs_create_export: failed to lookup for FSAL %s", fs_specific ) ;
           return fsalstat( ERR_FSAL_INVAL, EINVAL ) ;
       }

       expres = fsal_stack->ops->create_export( fsal_stack,
                                                export_path,
                                                fs_specific,
                                                exp_entry,
                                                NULL,
                                                up_ops,
                                                export ) ;
      
        if( FSAL_IS_ERROR( expres ) ) 
         {
	   LogMajor(COMPONENT_FSAL,
	            "nullfs_create_export: failed to call create_export on underlying FSAL %s", fs_specific ) ;
           return expres ;
         }

        /* Init next_ops structure */
        next_ops.exp_ops = gsh_malloc( sizeof( struct export_ops ) ) ;
        next_ops.obj_ops = gsh_malloc( sizeof( struct fsal_obj_ops ) ) ;
        next_ops.ds_ops  = gsh_malloc( sizeof( struct fsal_ds_ops ) ) ;

        memcpy( next_ops.exp_ops, (*export)->ops,     sizeof( struct export_ops ) ) ;
        memcpy( next_ops.obj_ops, (*export)->obj_ops, sizeof( struct fsal_obj_ops ) ) ;
        memcpy( next_ops.ds_ops,  (*export)->ds_ops,  sizeof( struct fsal_ds_ops ) ) ;
        next_ops.up_ops  = up_ops ;

        /* End of tmp code */

	nullfs_export_ops_init( (*export)->ops ) ;
	nullfs_handle_ops_init( (*export)->obj_ops );
       
	/* lock myself before attaching to the fsal.
	 * keep myself locked until done with creating myself.
	 */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


