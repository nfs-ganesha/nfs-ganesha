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
#include <nfs_exports.h>

static fsal_status_t
pxy_release(struct fsal_export *exp_hdl)
{
        struct pxy_export *pxy_exp =
                container_of(exp_hdl, struct pxy_export, exp);

        pthread_mutex_lock(&exp_hdl->lock);
        if(exp_hdl->refs > 0 || !glist_empty(&exp_hdl->handles)) {
                pthread_mutex_unlock(&exp_hdl->lock);
                ReturnCode(ERR_FSAL_INVAL, EBUSY);
        }
        fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
        pthread_mutex_unlock(&exp_hdl->lock);

        pthread_mutex_destroy(&exp_hdl->lock);
        free(pxy_exp);
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static fsal_boolean_t
pxy_get_supports(struct fsal_export *exp_hdl,
                fsal_fsinfo_options_t option)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_supports(&pm->fsinfo, option);
}

static fsal_size_t
pxy_get_maxfilesize(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_maxfilesize(&pm->fsinfo);
}

static fsal_size_t
pxy_get_maxread(struct fsal_export *exp_hdl)
{
        fsal_size_t sz;
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	sz = fsal_maxread(&pm->fsinfo);
        if (sz)
                return MIN(sz, exp_hdl->exp_entry->MaxRead);
        return exp_hdl->exp_entry->MaxRead;
}

static fsal_size_t
pxy_get_maxwrite(struct fsal_export *exp_hdl)
{
        fsal_size_t sz;
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	sz = fsal_maxwrite(&pm->fsinfo);
        if (sz)
                return MIN(sz, exp_hdl->exp_entry->MaxWrite);
        return exp_hdl->exp_entry->MaxWrite;
}

static fsal_count_t
pxy_get_maxlink(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_maxlink(&pm->fsinfo);
}

static fsal_mdsize_t
pxy_get_maxnamelen(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_maxnamelen(&pm->fsinfo);
}

static fsal_mdsize_t 
pxy_get_maxpathlen(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_maxpathlen(&pm->fsinfo);
}

static fsal_fhexptype_t
pxy_fh_expire_type(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_fh_expire_type(&pm->fsinfo);
}

static fsal_time_t
pxy_get_lease_time(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_lease_time(&pm->fsinfo);
}

static fsal_aclsupp_t
pxy_get_acl_support(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_acl_support(&pm->fsinfo);
}

static fsal_attrib_mask_t
pxy_get_supported_attrs(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_supported_attrs(&pm->fsinfo);
}

static fsal_accessmode_t
pxy_get_umask(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_umask(&pm->fsinfo);
}

static fsal_accessmode_t
pxy_get_xattr_access_rights(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_xattr_access_rights(&pm->fsinfo);
}

#ifdef _USE_FSALMDS
static fattr4_fs_layout_types 
pxy_get_layout_types(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_fs_layout_types(&pm->fsinfo);
}

static fsal_size_t 
pxy_layout_blksize(struct fsal_export *exp_hdl)
{
        struct pxy_fsal_module *pm =
                container_of(exp_hdl->fsal, struct pxy_fsal_module, module);
	return fsal_layout_blksize(&pm->fsinfo);
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

fsal_status_t 
pxy_lookup_junction(struct fsal_export *exp_hdl,
		    struct fsal_obj_handle *junction,
		    struct fsal_obj_handle **handle)
{
	ReturnCode(ERR_FSAL_NO_ERROR, 0);	
}

static struct export_ops pxy_exp_ops = {
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
        struct pxy_export *exp = calloc(1, sizeof(*exp));
        struct pxy_fsal_module *pxy =
                container_of(fsal_hdl, struct pxy_fsal_module, module);

        if (!exp)
                ReturnCode(ERR_FSAL_NOMEM, ENOMEM); 
        fsal_export_init(&exp->exp, &pxy_exp_ops, exp_entry);
        exp->info = &pxy->special;
        exp->exp.fsal = fsal_hdl;
        *export = &exp->exp;
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

