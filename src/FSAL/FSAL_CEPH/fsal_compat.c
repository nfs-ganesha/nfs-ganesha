/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Adam C. Emerson <aemerson@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_glue.h"
#include "fsal_internal.h"
#include "FSAL/common_methods.h"

fsal_functions_t fsal_ceph_functions = {
  .fsal_access = CEPHFSAL_access,
  .fsal_getattrs = CEPHFSAL_getattrs,
  .fsal_setattrs = CEPHFSAL_setattrs,
  .fsal_buildexportcontext = CEPHFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = CEPHFSAL_CleanUpExportContext,
  .fsal_initclientcontext = COMMON_InitClientContext,
  .fsal_getclientcontext = COMMON_GetClientContext,
  .fsal_create = CEPHFSAL_create,
  .fsal_mkdir = CEPHFSAL_mkdir,
  .fsal_link = CEPHFSAL_link,
  .fsal_mknode = CEPHFSAL_mknode,
  .fsal_opendir = CEPHFSAL_opendir,
  .fsal_readdir = CEPHFSAL_readdir,
  .fsal_closedir = CEPHFSAL_closedir,
  .fsal_open_by_name = CEPHFSAL_open_by_name,
  .fsal_open = CEPHFSAL_open,
  .fsal_read = CEPHFSAL_read,
  .fsal_write = CEPHFSAL_write,
  .fsal_commit = CEPHFSAL_commit,
  .fsal_close = CEPHFSAL_close,
  .fsal_open_by_fileid = COMMON_open_by_fileid,
  .fsal_close_by_fileid = COMMON_close_by_fileid,
  .fsal_dynamic_fsinfo = CEPHFSAL_dynamic_fsinfo,
  .fsal_init = CEPHFSAL_Init,
  .fsal_terminate = CEPHFSAL_terminate,
  .fsal_test_access = CEPHFSAL_test_access,
  .fsal_setattr_access = COMMON_setattr_access_notsupp,
  .fsal_rename_access = COMMON_rename_access,
  .fsal_create_access = COMMON_create_access,
  .fsal_unlink_access = COMMON_unlink_access,
  .fsal_link_access = COMMON_link_access,
  .fsal_merge_attrs = COMMON_merge_attrs,
  .fsal_lookup = CEPHFSAL_lookup,
  .fsal_lookuppath = CEPHFSAL_lookupPath,
  .fsal_lookupjunction = CEPHFSAL_lookupJunction,
  .fsal_cleanobjectresources = COMMON_CleanObjectResources,
  .fsal_set_quota = COMMON_set_quota_noquota,
  .fsal_get_quota = COMMON_get_quota_noquota,
  .fsal_check_quota = COMMON_check_quota,
  .fsal_rcp = CEPHFSAL_rcp,
  .fsal_rename = CEPHFSAL_rename,
  .fsal_get_stats = CEPHFSAL_get_stats,
  .fsal_readlink = CEPHFSAL_readlink,
  .fsal_symlink = CEPHFSAL_symlink,
  .fsal_handlecmp = CEPHFSAL_handlecmp,
  .fsal_handle_to_hashindex = CEPHFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = CEPHFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL,
  .fsal_digesthandle = CEPHFSAL_DigestHandle,
  .fsal_expandhandle = CEPHFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = CEPHFSAL_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter
  = CEPHFSAL_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter
  = CEPHFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf
  = CEPHFSAL_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf
  = CEPHFSAL_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf
  = CEPHFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = CEPHFSAL_truncate,
  .fsal_unlink = CEPHFSAL_unlink,
  .fsal_getfsname = CEPHFSAL_GetFSName,
  .fsal_getxattrattrs = CEPHFSAL_GetXAttrAttrs,
  .fsal_listxattrs = CEPHFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = CEPHFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = CEPHFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = CEPHFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = CEPHFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = CEPHFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = CEPHFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = CEPHFSAL_RemoveXAttrByName,
  .fsal_getextattrs = CEPHFSAL_getextattrs,
  .fsal_getfileno = CEPHFSAL_GetFileno,
  .fsal_share_op = COMMON_share_op_notsupp
};

fsal_const_t fsal_ceph_consts = {
  .fsal_handle_t_size = sizeof(cephfsal_handle_t),
  .fsal_op_context_t_size = sizeof(cephfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(cephfsal_export_context_t),
  .fsal_file_t_size = sizeof(cephfsal_file_t),
  .fsal_cookie_t_size = sizeof(cephfsal_cookie_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(cephfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(cephfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_ceph_functions;
} /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_ceph_consts;
}                               /* FSAL_GetConsts */
