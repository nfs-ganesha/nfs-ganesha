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

#include "config.h"

#include "fsal.h"
#include <string.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "hpss_methods.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "nfs_exports.h"

/*
 * VFS internal export
 */

struct hpss_fsal_export {
	struct fsal_export export;
        hpssfsal_export_context_t export_context;
};

/* helpers to/from other VFS objects
 */


hpssfsal_export_context_t * hpss_get_root_pvfs(struct fsal_export *exp_hdl) {
	struct hpss_fsal_export *myself;

	myself = container_of(exp_hdl, struct hpss_fsal_export, export);
	return &myself->export_context;
}

/* export object methods
 */

static fsal_status_t release(struct fsal_export *exp_hdl)
{
	struct hpss_fsal_export *myself;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	myself = container_of(exp_hdl, struct hpss_fsal_export, export);

	pthread_mutex_lock(&exp_hdl->lock);
	if(exp_hdl->refs > 0 || !glist_empty(&exp_hdl->handles)) {
		LogMajor(COMPONENT_FSAL,
			 "HPSS release: export (0x%p)busy",
			 exp_hdl);
		fsal_error = ERR_FSAL_DELAY;
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
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */

static fsal_status_t hpss_get_dynamic_info(struct fsal_export *exp_hdl,
                                           const struct req_op_context *opctx,
                                           fsal_dynamicfsinfo_t *dynamicinfo)
{

  /* sanity checks. */
  if( !dynamicinfo || !exp_hdl )
    {
       return fsalstat(ERR_FSAL_FAULT, 0);
    }

#ifdef BUGAZOMU
  hpss_statfs_t hpss_statfs;
  unsigned int cos_export;
  int rc;
  struct hpss_fsal_export *myself;
  hpssfsal_export_context_t *exp_context;

  myself = container_of(exp_hdl, struct hpss_fsal_export, export);
  exp_context = &myself->export_context;

  /* retrieves the default cos (or the user defined cos for this fileset) */

  if(exp_context->default_cos != 0)
    {
      cos_export = exp_context->default_cos;
    }
  else
    {
      /* retrieves default fileset cos */

      ns_FilesetAttrBits_t attrBits;
      ns_FilesetAttrs_t fsattrs;

      memset(&fsattrs, 0, sizeof(ns_FilesetAttrs_t));

      attrBits = cast64m(0);
      attrBits = orbit64m(attrBits, NS_FS_ATTRINDEX_COS);

      rc = hpss_FilesetGetAttributes(NULL, NULL,
                                     &exp_context->fileset_root_handle,
                                     NULL, attrBits, &fsattrs);

      if(rc)
        return fsalstat(hpss2fsal_error(rc), -rc);

      cos_export = fsattrs.ClassOfService;
      /* cache it */
      exp_context->default_cos = cos_export;

    }

  rc = hpss_Statfs(cos_export, &hpss_statfs);

  if(rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  /* @todo :  sometimes hpss_statfs.f_blocks < hpss_statfs.f_bfree !!! */

  if(dynamicinfo->total_bytes > dynamicinfo->free_bytes)
    {
      dynamicinfo->total_bytes = hpss_statfs.f_blocks * hpss_statfs.f_bsize;
      dynamicinfo->free_bytes = hpss_statfs.f_bfree * hpss_statfs.f_bsize;
      dynamicinfo->avail_bytes = hpss_statfs.f_bavail * hpss_statfs.f_bsize;

      dynamicinfo->total_files = hpss_statfs.f_files;
      dynamicinfo->free_files = hpss_statfs.f_ffree;
      dynamicinfo->avail_files = hpss_statfs.f_ffree;
    }
#else

  /* return dummy values... like HPSS do... */

  dynamicinfo->total_bytes = INT_MAX;
  dynamicinfo->free_bytes = INT_MAX;
  dynamicinfo->avail_bytes = INT_MAX;

  dynamicinfo->total_files = 20000000;
  dynamicinfo->free_files = 1000000;
  dynamicinfo->avail_files = 1000000;

#endif
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
				    struct gsh_buffdesc *fh_desc)
{
  /* sanity checks */
  if( !fh_desc || !fh_desc->addr)
    return fsalstat(ERR_FSAL_FAULT, 0);

  if(in_type == FSAL_DIGEST_SIZEOF)
    return fsalstat(ERR_FSAL_NOTSUPP, 0);

  if(fh_desc->len != sizeof(ns_ObjHandle_t)) {
    LogMajor(COMPONENT_FSAL,
       "Size mismatch for handle.  should be %lu, got %u",
       (unsigned long int)sizeof(ns_ObjHandle_t), (unsigned int)fh_desc->len);
    return fsalstat(ERR_FSAL_SERVERFAULT, 0);
  }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

void hpss_handle_ops_init(struct fsal_obj_ops *ops);

/* hpss_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void hpss_export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = hpss_lookup_path; 
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


/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t hpss_create_export(struct fsal_module *fsal_hdl,
                                   const char *export_path,
                                   const char *fs_options,
                                   exportlist_t *exp_entry,
                                   struct fsal_module *next_fsal,
                                   const struct fsal_up_vector *up_ops,
                                   struct fsal_export **export)
{
	struct hpss_fsal_export *myself;
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	*export = NULL; /* poison it first */
	if(export_path == NULL
	   || strlen(export_path) == 0
	   || strlen(export_path) > MAXPATHLEN) {
		LogMajor(COMPONENT_FSAL,
			 "hpss_create_export: export path empty or too big");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}
	if(next_fsal != NULL) {
		LogCrit(COMPONENT_FSAL,
			"This module is not stackable");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	myself = malloc(sizeof(struct hpss_fsal_export));
	if(myself == NULL) {
		LogMajor(COMPONENT_FSAL,
			 "hpss_fsal_create: out of memory for object");
		return fsalstat(ERR_FSAL_NOMEM, errno);
	}
	memset(myself, 0, sizeof(struct hpss_fsal_export));

                
        retval = fsal_export_init(&myself->export, exp_entry);
	if(retval != 0)
		goto errout;

	hpss_export_ops_init(myself->export.ops);
	hpss_handle_ops_init(myself->export.obj_ops);
        myself->export.up_ops = up_ops;
	/* lock myself before attaching to the fsal.
	 * keep myself locked until done with creating myself.
	 */

	pthread_mutex_lock(&myself->export.lock);
	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);
	if(retval != 0)
		goto errout; /* seriously bad */
	myself->export.fsal = fsal_hdl;

        // HPSS_INIT to add here

      	*export = &myself->export;
        pthread_mutex_unlock(&myself->export.lock);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

errout:
	myself->export.ops = NULL; /* poison myself */
	pthread_mutex_unlock(&myself->export.lock);
	pthread_mutex_destroy(&myself->export.lock);
	free(myself);  /* elvis has left the building */
	return fsalstat(fsal_error, retval);
}


