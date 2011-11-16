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

fsal_functions_t fsal_xfs_functions = {
  .fsal_access = XFSFSAL_access,
  .fsal_getattrs = XFSFSAL_getattrs,
  .fsal_setattrs = XFSFSAL_setattrs,
  .fsal_buildexportcontext = XFSFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = COMMON_CleanUpExportContext_noerror,
  .fsal_initclientcontext = COMMON_InitClientContext,
  .fsal_getclientcontext = COMMON_GetClientContext,
  .fsal_create = XFSFSAL_create,
  .fsal_mkdir = XFSFSAL_mkdir,
  .fsal_link = XFSFSAL_link,
  .fsal_mknode = XFSFSAL_mknode,
  .fsal_opendir = XFSFSAL_opendir,
  .fsal_readdir = XFSFSAL_readdir,
  .fsal_closedir = XFSFSAL_closedir,
  .fsal_open_by_name = XFSFSAL_open_by_name,
  .fsal_open = XFSFSAL_open,
  .fsal_read = XFSFSAL_read,
  .fsal_write = XFSFSAL_write,
  .fsal_close = XFSFSAL_close,
  .fsal_open_by_fileid = COMMON_open_by_fileid,
  .fsal_close_by_fileid = COMMON_close_by_fileid,
  .fsal_dynamic_fsinfo = XFSFSAL_dynamic_fsinfo,
  .fsal_init = XFSFSAL_Init,
  .fsal_terminate = COMMON_terminate_noerror,
  .fsal_test_access = XFSFSAL_test_access,
  .fsal_setattr_access = COMMON_setattr_access_notsupp,
  .fsal_rename_access = COMMON_rename_access,
  .fsal_create_access = COMMON_create_access,
  .fsal_unlink_access = COMMON_unlink_access,
  .fsal_link_access = COMMON_link_access,
  .fsal_merge_attrs = COMMON_merge_attrs,
  .fsal_lookup = XFSFSAL_lookup,
  .fsal_lookuppath = XFSFSAL_lookupPath,
  .fsal_lookupjunction = XFSFSAL_lookupJunction,
  .fsal_cleanobjectresources = COMMON_CleanObjectResources,
  .fsal_set_quota = XFSFSAL_set_quota,
  .fsal_get_quota = XFSFSAL_get_quota,
  .fsal_rcp = XFSFSAL_rcp,
  .fsal_rcp_by_fileid = COMMON_rcp_by_fileid,
  .fsal_rename = XFSFSAL_rename,
  .fsal_get_stats = XFSFSAL_get_stats,
  .fsal_readlink = XFSFSAL_readlink,
  .fsal_symlink = XFSFSAL_symlink,
  .fsal_sync = XFSFSAL_sync,
  .fsal_handlecmp = XFSFSAL_handlecmp,
  .fsal_handle_to_hashindex = XFSFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = XFSFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL, 
  .fsal_digesthandle = XFSFSAL_DigestHandle,
  .fsal_expandhandle = XFSFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = XFSFSAL_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = XFSFSAL_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter = XFSFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = XFSFSAL_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      XFSFSAL_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      XFSFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = XFSFSAL_truncate,
  .fsal_unlink = XFSFSAL_unlink,
  .fsal_getfsname = XFSFSAL_GetFSName,
  .fsal_getxattrattrs = XFSFSAL_GetXAttrAttrs,
  .fsal_listxattrs = XFSFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = XFSFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = XFSFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = XFSFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = XFSFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = XFSFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = XFSFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = XFSFSAL_RemoveXAttrByName,
  .fsal_getextattrs = XFSFSAL_getextattrs,
  .fsal_getfileno = XFSFSAL_GetFileno
};

fsal_const_t fsal_xfs_consts = {
  .fsal_handle_t_size = sizeof(xfsfsal_handle_t),
  .fsal_op_context_t_size = sizeof(xfsfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(xfsfsal_export_context_t),
  .fsal_file_t_size = sizeof(xfsfsal_file_t),
  .fsal_cookie_t_size = sizeof(xfsfsal_cookie_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(xfsfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(xfsfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_xfs_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_xfs_consts;
}                               /* FSAL_GetConsts */
