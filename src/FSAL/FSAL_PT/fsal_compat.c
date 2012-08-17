// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_compat.c
// Description: FSAL compatibility operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_glue.h"
#include "fsal_internal.h"
#include "FSAL/common_methods.h"

fsal_functions_t fsal_ptfs_functions = {
  .fsal_access = PTFSAL_access,
  .fsal_getattrs = PTFSAL_getattrs,
  .fsal_getattrs_descriptor = PTFSAL_getattrs_descriptor,
  .fsal_setattrs = PTFSAL_setattrs,
  .fsal_buildexportcontext = PTFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = PTFSAL_CleanUpExportContext,
  .fsal_initclientcontext = COMMON_InitClientContext,
  .fsal_getclientcontext = COMMON_GetClientContext,
  .fsal_create = PTFSAL_create,
  .fsal_mkdir = PTFSAL_mkdir,
  .fsal_link = PTFSAL_link,
  .fsal_mknode = PTFSAL_mknode,
  .fsal_opendir = PTFSAL_opendir,
  .fsal_readdir = PTFSAL_readdir,
  .fsal_closedir = PTFSAL_closedir,
  .fsal_open_by_name = PTFSAL_open_by_name,
  .fsal_open = PTFSAL_open,
  .fsal_read = PTFSAL_read,
  .fsal_write = PTFSAL_write,
  .fsal_commit = PTFSAL_commit,
  .fsal_close = PTFSAL_close,
  .fsal_open_by_fileid = COMMON_open_by_fileid,
  .fsal_close_by_fileid = COMMON_close_by_fileid,
  .fsal_dynamic_fsinfo = PTFSAL_dynamic_fsinfo,
  .fsal_init = PTFSAL_Init,
  .fsal_terminate = PTFSAL_terminate,
  .fsal_test_access = PTFSAL_test_access,
  .fsal_setattr_access = COMMON_setattr_access_notsupp,
  .fsal_rename_access = COMMON_rename_access,
  .fsal_create_access = COMMON_create_access,
  .fsal_unlink_access = COMMON_unlink_access,
  .fsal_link_access = COMMON_link_access,
  .fsal_merge_attrs = COMMON_merge_attrs,
  .fsal_lookup = PTFSAL_lookup,
  .fsal_lookuppath = PTFSAL_lookupPath,
  .fsal_lookupjunction = PTFSAL_lookupJunction,
  .fsal_lock_op = NULL,
  .fsal_cleanobjectresources = COMMON_CleanObjectResources,
  .fsal_set_quota = COMMON_set_quota_noquota,
  .fsal_get_quota = COMMON_get_quota_noquota,
  .fsal_check_quota = COMMON_check_quota,
  .fsal_rcp = PTFSAL_rcp,
  .fsal_rename = PTFSAL_rename,
  .fsal_get_stats = PTFSAL_get_stats,
  .fsal_readlink = PTFSAL_readlink,
  .fsal_symlink = PTFSAL_symlink,
  .fsal_handlecmp = PTFSAL_handlecmp,
  .fsal_handle_to_hashindex = PTFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = PTFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL, 
  .fsal_digesthandle = PTFSAL_DigestHandle,
  .fsal_expandhandle = PTFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = COMMON_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = COMMON_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter = PTFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = COMMON_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      COMMON_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      PTFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = PTFSAL_truncate,
  .fsal_unlink = PTFSAL_unlink,
  .fsal_getfsname = PTFSAL_GetFSName,
  .fsal_getxattrattrs = PTFSAL_GetXAttrAttrs,
  .fsal_listxattrs = PTFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = PTFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = PTFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = PTFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = PTFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = PTFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = PTFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = PTFSAL_RemoveXAttrByName,
  .fsal_getextattrs = COMMON_getextattrs_notsupp,
  .fsal_getfileno = PTFSAL_GetFileno,
};

fsal_const_t fsal_ptfs_consts = {
  .fsal_handle_t_size = sizeof(ptfsal_handle_t),
  .fsal_op_context_t_size = sizeof(ptfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(ptfsal_export_context_t),
  .fsal_file_t_size = sizeof(ptfsal_file_t),
  .fsal_cookie_t_size = sizeof(ptfsal_cookie_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(ptfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(ptfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_ptfs_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_ptfs_consts;
}                               /* FSAL_GetConsts */
