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

/* Export-related methods */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <pthread.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "pxy_fsal_methods.h"

static fsal_status_t
pxy_release(struct fsal_export *exp_hdl)
{
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_get_dynamic_info(struct fsal_export *exp_hdl,
                     fsal_dynamicfsinfo_t *infop)
{
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static fsal_boolean_t
pxy_get_supports(struct fsal_export *exp_hdl,
                fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_supports(info, option);
}

static fsal_size_t
pxy_get_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_maxfilesize(info);
}

static fsal_size_t
pxy_get_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_maxread(info);
}

static fsal_size_t
pxy_get_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_maxwrite(info);
}

static fsal_count_t
pxy_get_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_maxlink(info);
}

static fsal_mdsize_t
pxy_get_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_maxnamelen(info);
}

static fsal_mdsize_t 
pxy_get_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_maxpathlen(info);
}

static fsal_fhexptype_t
pxy_fh_expire_type(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_fh_expire_type(info);
}

static fsal_time_t
pxy_get_lease_time(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_lease_time(info);
}

static fsal_aclsupp_t
pxy_get_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_acl_support(info);
}

static fsal_attrib_mask_t
pxy_get_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_supported_attrs(info);
}

static fsal_accessmode_t
pxy_get_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_umask(info);
}

static fsal_accessmode_t
pxy_get_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_xattr_access_rights(info);
}

#ifdef _USE_FSALMDS
static fattr4_fs_layout_types 
pxy_get_layout_types(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_fs_layout_types(info);
}

static fsal_size_t 
pxy_layout_blksize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info = NULL;
	return fsal_layout_blksize(info);
}
#endif

static fsal_status_t
pxy_check_quota(struct fsal_export *exp_hdl,
                const char * filepath,
		int quota_type,
		struct user_cred *creds)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0) ;
}

static fsal_status_t
pxy_get_quota(struct fsal_export *exp_hdl,
	  const char * filepath,
	  int quota_type,
	  struct user_cred *creds,
	  fsal_quota_t *pquota)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0) ;
}

static fsal_status_t
pxy_set_quota(struct fsal_export *exp_hdl,
	      const char *filepath,
	      int quota_type,
	      struct user_cred *creds,
	      fsal_quota_t * pquota,
	      fsal_quota_t * presquota)
{
	ReturnCode(ERR_FSAL_NOTSUPP, 0) ;
}

static fsal_status_t
pxy_extract_handle(struct fsal_export *exp_hdl,
		   fsal_digesttype_t in_type,
		   struct fsal_handle_desc *fh_desc)
{
	ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t 
pxy_lookup_junction(struct fsal_export *exp_hdl,
		    struct fsal_obj_handle *junction,
		    struct fsal_obj_handle **handle)
{
	ReturnCode(ERR_FSAL_NO_ERROR, 0);	
}

 struct export_ops pxy_exp_ops = {
	.get = fsal_export_get,
	.put = fsal_export_put,
	.release = pxy_release,
	.lookup_path = pxy_lookup_path,
	.lookup_junction = pxy_lookup_junction,
	.extract_handle = pxy_extract_handle,
	.create_handle = pxy_create_handle,
	.get_fs_dynamic_info = pxy_get_dynamic_info,
	.fs_supports = pxy_get_supports,
	.fs_maxfilesize = pxy_get_maxfilesize,
	.fs_maxread = pxy_get_maxread,
	.fs_maxwrite = pxy_get_maxwrite,
	.fs_maxlink = pxy_get_maxlink,
	.fs_maxnamelen = pxy_get_maxnamelen,
	.fs_maxpathlen = pxy_get_maxpathlen,
	.fs_fh_expire_type = pxy_fh_expire_type,
	.fs_lease_time = pxy_get_lease_time,
	.fs_acl_support = pxy_get_acl_support,
	.fs_supported_attrs = pxy_get_supported_attrs,
	.fs_umask = pxy_get_umask,
	.fs_xattr_access_rights = pxy_get_xattr_access_rights,
#ifdef _USE_FSALMDS
	.fs_layout_types = pxy_get_layout_types,
	.layout_blksize = pxy_layout_blksize,
#endif
	.check_quota = pxy_check_quota,
	.get_quota = pxy_get_quota,
	.set_quota = pxy_set_quota
};

/* Here and not static because proxy.c needs this function
 * but we also need access to pxy_exp_ops - I'd rather
 * keep the later static then the former */
fsal_status_t
pxy_create_export(struct fsal_module *fsal_hdl,
                  const char *export_path,
                  const char *fs_options,
                  struct exportlist__ *exp_entry,
                  struct fsal_module *next_fsal,
                  struct fsal_export **export)
{
        struct fsal_export *exp = calloc(1, sizeof(*exp));

        if (!exp)
                ReturnCode(ERR_FSAL_NOMEM, ENOMEM); 
        fsal_export_init(exp, &pxy_exp_ops, exp_entry);
        *export = exp;
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

