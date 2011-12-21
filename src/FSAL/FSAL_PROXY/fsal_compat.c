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

#include "fsal_types.h"
#include "fsal_glue.h"
#include "fsal_internal.h"
#include "FSAL/common_methods.h"

fsal_functions_t fsal_proxy_functions = {
  .fsal_access = PROXYFSAL_access,
  .fsal_getattrs = PROXYFSAL_getattrs,
  .fsal_setattrs = PROXYFSAL_setattrs,
  .fsal_buildexportcontext = PROXYFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = COMMON_CleanUpExportContext_noerror,
  .fsal_initclientcontext = PROXYFSAL_InitClientContext,
  .fsal_getclientcontext = COMMON_GetClientContext,
  .fsal_create = PROXYFSAL_create,
  .fsal_mkdir = PROXYFSAL_mkdir,
  .fsal_link = PROXYFSAL_link,
  .fsal_mknode = PROXYFSAL_mknode,
  .fsal_opendir = PROXYFSAL_opendir,
  .fsal_readdir = PROXYFSAL_readdir,
  .fsal_closedir = PROXYFSAL_closedir,
  .fsal_open_by_name = PROXYFSAL_open_by_name,
  .fsal_open = PROXYFSAL_open,
  .fsal_read = PROXYFSAL_read,
  .fsal_write = PROXYFSAL_write,
  .fsal_sync = PROXYFSAL_sync,
  .fsal_close = PROXYFSAL_close,
  .fsal_open_by_fileid = PROXYFSAL_open_by_fileid,
  .fsal_close_by_fileid = PROXYFSAL_close_by_fileid,
  .fsal_dynamic_fsinfo = PROXYFSAL_dynamic_fsinfo,
  .fsal_init = PROXYFSAL_Init,
  .fsal_terminate = PROXYFSAL_terminate,
  .fsal_test_access = PROXYFSAL_test_access,
  .fsal_setattr_access = PROXYFSAL_setattr_access,
  .fsal_rename_access = COMMON_rename_access,
  .fsal_create_access = COMMON_create_access,
  .fsal_unlink_access = COMMON_unlink_access,
  .fsal_link_access = COMMON_link_access,
  .fsal_merge_attrs = COMMON_merge_attrs,
  .fsal_lookup = PROXYFSAL_lookup,
  .fsal_lookuppath = PROXYFSAL_lookupPath,
  .fsal_lookupjunction = PROXYFSAL_lookupJunction,
  .fsal_cleanobjectresources = COMMON_CleanObjectResources,
  .fsal_set_quota = COMMON_set_quota_noquota,
  .fsal_get_quota = COMMON_get_quota_noquota,
  .fsal_rcp = PROXYFSAL_rcp,
  .fsal_rcp_by_fileid = PROXYFSAL_rcp_by_fileid,
  .fsal_rename = PROXYFSAL_rename,
  .fsal_get_stats = PROXYFSAL_get_stats,
  .fsal_readlink = PROXYFSAL_readlink,
  .fsal_symlink = PROXYFSAL_symlink,
  .fsal_handlecmp = PROXYFSAL_handlecmp,
  .fsal_handle_to_hashindex = PROXYFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = PROXYFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL, 
  .fsal_digesthandle = PROXYFSAL_DigestHandle,
  .fsal_expandhandle = PROXYFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = PROXYFSAL_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = PROXYFSAL_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter =
      PROXYFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = PROXYFSAL_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      PROXYFSAL_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      PROXYFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = PROXYFSAL_truncate,
  .fsal_unlink = PROXYFSAL_unlink,
  .fsal_getfsname = PROXYFSAL_GetFSName,
  .fsal_getxattrattrs = PROXYFSAL_GetXAttrAttrs,
  .fsal_listxattrs = PROXYFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = PROXYFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = PROXYFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = PROXYFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = PROXYFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = PROXYFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = PROXYFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = PROXYFSAL_RemoveXAttrByName,
  .fsal_getextattrs = COMMON_getextattrs_notsupp,
  .fsal_getfileno = PROXYFSAL_GetFileno
};

fsal_const_t fsal_proxy_consts = {
  .fsal_handle_t_size = sizeof(proxyfsal_handle_t),
  .fsal_op_context_t_size = sizeof(proxyfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(proxyfsal_export_context_t),
  .fsal_file_t_size = sizeof(proxyfsal_file_t),
  .fsal_cookie_t_size = sizeof(proxyfsal_cookie_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(proxyfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(proxyfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_proxy_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_proxy_consts;
}                               /* FSAL_GetConsts */
