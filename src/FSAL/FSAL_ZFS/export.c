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

/* export.c
 * VFS FSAL export object
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <string.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "zfs_methods.h"
#include "FSAL/FSAL_ZFS/fsal_types.h"

libzfswrap_handle_t *p_zhd = NULL ;
size_t i_snapshots = 0 ;
snapshot_t *p_snapshots = NULL ;


/*
 * VFS internal export
 */

struct zfs_fsal_export {
	struct fsal_export export;
        libzfswrap_vfs_t *p_vfs;
};

/* helpers to/from other VFS objects
 */

struct fsal_staticfsinfo_t *zfs_staticinfo(struct fsal_module *hdl);

libzfswrap_vfs_t * zfs_get_root_pvfs(struct fsal_export *exp_hdl) {
	struct zfs_fsal_export *myself;

	myself = container_of(exp_hdl, struct zfs_fsal_export, export);
	return myself->p_vfs;
}

/* export object methods
 */

static fsal_status_t release(struct fsal_export *exp_hdl)
{
	struct zfs_fsal_export *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(exp_hdl, struct zfs_fsal_export, export);

	pthread_mutex_lock(&exp_hdl->lock);
	if(exp_hdl->refs > 0 || !glist_empty(&exp_hdl->handles)) {
		LogMajor(COMPONENT_FSAL,
			 "ZFS release: export (0x%p)busy",
			 exp_hdl);
		fsal_error = posix2fsal_error(EBUSY);
		retval = EBUSY;
		goto errout;
	}
	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);
	myself->export.ops = NULL; /* poison myself */
	pthread_mutex_unlock(&exp_hdl->lock);

	pthread_mutex_destroy(&exp_hdl->lock);
	free(myself);  /* elvis has left the building */
	return fsalstat(fsal_error, retval);

errout:
	pthread_mutex_unlock(&exp_hdl->lock);
	return fsalstat(fsal_error, retval);
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      const struct req_op_context *opctx,
				      fsal_dynamicfsinfo_t *infop)
{
	struct zfs_fsal_export *myself;
         struct statvfs statfs;
        
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	if( !infop) {
		fsal_error = ERR_FSAL_FAULT;
		goto out;
	}
	myself = container_of(exp_hdl, struct zfs_fsal_export, export);
        retval = libzfswrap_statfs( myself->p_vfs,
                                    &statfs);
        
	if(retval < 0) {
		fsal_error = posix2fsal_error(errno);
		retval = errno;
		goto out;
	}

        infop->total_bytes = statfs.f_frsize * statfs.f_blocks;
        infop->free_bytes = statfs.f_frsize * statfs.f_bfree;
        infop->avail_bytes = statfs.f_frsize * statfs.f_bavail;

        infop->total_files = statfs.f_files;
        infop->free_files = statfs.f_ffree;
        infop->avail_files = statfs.f_favail;

        infop->time_delta.seconds = 1;
        infop->time_delta.nseconds = 0;
        
out:
	return fsalstat(fsal_error, retval);	
}

static bool fs_supports(struct fsal_export *exp_hdl,
                        fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

static fsal_fhexptype_t fs_fh_expire_type(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_fh_expire_type(info);
}

static gsh_time_t fs_lease_time(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_lease_time(info);
}

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_acl_support(info);
}

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_supported_attrs(info);
}

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

static uint32_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = zfs_staticinfo(exp_hdl->fsal);
	return fsal_xattr_access_rights(info);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t extract_handle(struct fsal_export *exp_hdl,
				    fsal_digesttype_t in_type,
				    struct netbuf *fh_desc)
{
	struct zfs_file_handle *hdl;
	size_t fh_size;

	/* sanity checks */
	if( !fh_desc || !fh_desc->buf)
		return fsalstat(ERR_FSAL_FAULT, 0);

	hdl = (struct zfs_file_handle *)fh_desc->buf;
	fh_size = zfs_sizeof_handle(hdl);
	if(in_type == FSAL_DIGEST_NFSV2) {
		if(fh_desc->len < fh_size) {
			LogMajor(COMPONENT_FSAL,
				 "V2 size too small for handle.  should be %lu, got %u",
				 fh_size, fh_desc->len);
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}
	} else if(in_type != FSAL_DIGEST_SIZEOF && fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %u",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = fh_size;  /* pass back the actual size */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

void zfs_handle_ops_init(struct fsal_obj_ops *ops);

/* zfs_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void zfs_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = zfs_lookup_path; 
	ops->extract_handle = extract_handle;
	ops->create_handle = zfs_create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->fs_supports = fs_supports;
	ops->fs_maxfilesize = fs_maxfilesize;
	ops->fs_maxread = fs_maxread;
	ops->fs_maxwrite = fs_maxwrite;
	ops->fs_maxlink = fs_maxlink;
	ops->fs_maxnamelen = fs_maxnamelen;
	ops->fs_maxpathlen = fs_maxpathlen;
	ops->fs_fh_expire_type = fs_fh_expire_type;
	ops->fs_lease_time = fs_lease_time;
	ops->fs_acl_support = fs_acl_support;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->fs_umask = fs_umask;
	ops->fs_xattr_access_rights = fs_xattr_access_rights;
}

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t zfs_create_export(struct fsal_module *fsal_hdl,
                                   const char *export_path,
                                   const char *fs_options,
                                   struct exportlist__ *exp_entry,
                                   struct fsal_module *next_fsal,
                                   const struct fsal_up_vector *up_ops,
                                   struct fsal_export **export)
{
	struct zfs_fsal_export *myself;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
        libzfswrap_vfs_t *p_zfs = NULL ;

	*export = NULL; /* poison it first */
	if(export_path == NULL
	   || strlen(export_path) == 0
	   || strlen(export_path) > MAXPATHLEN) {
		LogMajor(COMPONENT_FSAL,
			 "zfs_create_export: export path empty or too big");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}
	if(next_fsal != NULL) {
		LogCrit(COMPONENT_FSAL,
			"This module is not stackable");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	myself = malloc(sizeof(struct zfs_fsal_export));
	if(myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "zfs_fsal_create: out of memory for object");
		return fsalstat(posix2fsal_error(errno), errno);
	}
	memset(myself, 0, sizeof(struct zfs_fsal_export));

        if( p_zhd == NULL )
        {
          /* init libzfs library */
          if( ( p_zhd = libzfswrap_init() ) == NULL )
           {
             fprintf( stderr, "Could not init libzfswrap library");
	     return fsalstat(ERR_FSAL_INVAL, 0);
           }

        }

        if( p_snapshots == NULL )
        {
          /* Mount the libs */
          if( ( p_zfs = libzfswrap_mount(fs_options, "/tank", "") ) == NULL )
            {
              fprintf( stderr, "Could not mount libzfswrapp");
	      return fsalstat(ERR_FSAL_INVAL, 0);
            }

          /** @todo: Place snapshot management here */
          p_snapshots = calloc(1, sizeof(*p_snapshots));
          p_snapshots[0].p_vfs = p_zfs ;
          i_snapshots = 0 ; 
        }

        myself->p_vfs = p_snapshots[0].p_vfs;
        
        retval = fsal_export_init(&myself->export, exp_entry);
	if(retval != 0)
		goto errout;

	zfs_export_ops_init(myself->export.ops);
	zfs_handle_ops_init(myself->export.obj_ops);
        myself->export.up_ops = up_ops;
	/* lock myself before attaching to the fsal.
	 * keep myself locked until done with creating myself.
	 */

	pthread_mutex_lock(&myself->export.lock);
	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);
	if(retval != 0)
		goto errout; /* seriously bad */
	myself->export.fsal = fsal_hdl;

	*export = &myself->export;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

errout:
	myself->export.ops = NULL; /* poison myself */
	pthread_mutex_unlock(&myself->export.lock);
	pthread_mutex_destroy(&myself->export.lock);
	free(myself);  /* elvis has left the building */
	return fsalstat(fsal_error, retval);
}


