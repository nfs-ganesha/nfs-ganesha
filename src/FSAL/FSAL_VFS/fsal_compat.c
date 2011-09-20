/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 * \file    fsal_compat.c
 * \brief   FSAL glue functions
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_glue.h"
#include "fsal_internal.h"
#include "FSAL/common_methods.h"

fsal_functions_t fsal_vfs_functions = {
  .fsal_access = VFSFSAL_access,
  .fsal_getattrs = VFSFSAL_getattrs,
  .fsal_getattrs_descriptor = VFSFSAL_getattrs_descriptor,
  .fsal_setattrs = VFSFSAL_setattrs,
  .fsal_buildexportcontext = VFSFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = COMMON_CleanUpExportContext_noerror,
  .fsal_initclientcontext = COMMON_InitClientContext,
  .fsal_getclientcontext = COMMON_GetClientContext,
  .fsal_create = VFSFSAL_create,
  .fsal_mkdir = VFSFSAL_mkdir,
  .fsal_link = VFSFSAL_link,
  .fsal_mknode = VFSFSAL_mknode,
  .fsal_opendir = VFSFSAL_opendir,
  .fsal_readdir = VFSFSAL_readdir,
  .fsal_closedir = VFSFSAL_closedir,
  .fsal_open_by_name = VFSFSAL_open_by_name,
  .fsal_open = VFSFSAL_open,
  .fsal_read = VFSFSAL_read,
  .fsal_write = VFSFSAL_write,
  .fsal_sync = VFSFSAL_sync,
  .fsal_close = VFSFSAL_close,
  .fsal_open_by_fileid = COMMON_open_by_fileid,
  .fsal_close_by_fileid = COMMON_close_by_fileid,
  .fsal_dynamic_fsinfo = VFSFSAL_dynamic_fsinfo,
  .fsal_init = VFSFSAL_Init,
  .fsal_terminate = COMMON_terminate_noerror,
  .fsal_test_access = VFSFSAL_test_access,
  .fsal_setattr_access = COMMON_setattr_access_notsupp,
  .fsal_rename_access = COMMON_rename_access,
  .fsal_create_access = COMMON_create_access,
  .fsal_unlink_access = COMMON_unlink_access,
  .fsal_link_access = COMMON_link_access,
  .fsal_merge_attrs = COMMON_merge_attrs,
  .fsal_lookup = VFSFSAL_lookup,
  .fsal_lookuppath = VFSFSAL_lookupPath,
  .fsal_lookupjunction = VFSFSAL_lookupJunction,
  .fsal_lock_op = VFSFSAL_lock_op,
  .fsal_cleanobjectresources = COMMON_CleanObjectResources,
  .fsal_set_quota = COMMON_set_quota_noquota,
  .fsal_get_quota = COMMON_get_quota_noquota,
  .fsal_rcp = VFSFSAL_rcp,
  .fsal_rcp_by_fileid = COMMON_rcp_by_fileid,
  .fsal_rename = VFSFSAL_rename,
  .fsal_get_stats = VFSFSAL_get_stats,
  .fsal_readlink = VFSFSAL_readlink,
  .fsal_symlink = VFSFSAL_symlink,
  .fsal_handlecmp = VFSFSAL_handlecmp,
  .fsal_handle_to_hashindex = VFSFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = VFSFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL, 
  .fsal_digesthandle = VFSFSAL_DigestHandle,
  .fsal_expandhandle = VFSFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = VFSFSAL_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = VFSFSAL_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter = VFSFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = VFSFSAL_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      VFSFSAL_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      VFSFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = VFSFSAL_truncate,
  .fsal_unlink = VFSFSAL_unlink,
  .fsal_getfsname = VFSFSAL_GetFSName,
  .fsal_getxattrattrs = VFSFSAL_GetXAttrAttrs,
  .fsal_listxattrs = VFSFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = VFSFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = VFSFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = VFSFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = VFSFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = VFSFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = VFSFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = VFSFSAL_RemoveXAttrByName,
  .fsal_getextattrs = COMMON_getextattrs_notsupp,
  .fsal_getfileno = VFSFSAL_GetFileno
};

fsal_const_t fsal_vfs_consts = {
  .fsal_handle_t_size = sizeof(vfsfsal_handle_t),
  .fsal_op_context_t_size = sizeof(vfsfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(vfsfsal_export_context_t),
  .fsal_file_t_size = sizeof(vfsfsal_file_t),
  .fsal_cookie_t_size = sizeof(vfsfsal_cookie_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(vfsfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(vfsfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_vfs_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_vfs_consts;
}                               /* FSAL_GetConsts */
