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

#include <string.h>             /* For memcpy */

#include "fsal.h"
#include "fsal_glue.h"
#include "fsal_internal.h"
#include "FSAL/common_methods.h"

fsal_functions_t fsal_fuse_functions = {
  .fsal_access = FUSEFSAL_access,
  .fsal_getattrs = FUSEFSAL_getattrs,
  .fsal_setattrs = FUSEFSAL_setattrs,
  .fsal_buildexportcontext = FUSEFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = COMMON_CleanUpExportContext_noerror,
  .fsal_initclientcontext = FUSEFSAL_InitClientContext,
  .fsal_getclientcontext = FUSEFSAL_GetClientContext,
  .fsal_create = FUSEFSAL_create,
  .fsal_mkdir = FUSEFSAL_mkdir,
  .fsal_link = FUSEFSAL_link,
  .fsal_mknode = FUSEFSAL_mknode,
  .fsal_opendir = FUSEFSAL_opendir,
  .fsal_readdir = FUSEFSAL_readdir,
  .fsal_closedir = FUSEFSAL_closedir,
  .fsal_open_by_name = FUSEFSAL_open_by_name,
  .fsal_open = FUSEFSAL_open,
  .fsal_read = FUSEFSAL_read,
  .fsal_write = FUSEFSAL_write,
  .fsal_close = FUSEFSAL_close,
  .fsal_open_by_fileid = COMMON_open_by_fileid,
  .fsal_close_by_fileid = COMMON_close_by_fileid,
  .fsal_dynamic_fsinfo = FUSEFSAL_dynamic_fsinfo,
  .fsal_init = FUSEFSAL_Init,
  .fsal_terminate = COMMON_terminate_noerror,
  .fsal_test_access = FUSEFSAL_test_access,
  .fsal_setattr_access = COMMON_setattr_access_notsupp,
  .fsal_rename_access = COMMON_rename_access,
  .fsal_create_access = COMMON_create_access,
  .fsal_unlink_access = COMMON_unlink_access,
  .fsal_link_access = COMMON_link_access,
  .fsal_merge_attrs = COMMON_merge_attrs,
  .fsal_lookup = FUSEFSAL_lookup,
  .fsal_lookuppath = FUSEFSAL_lookupPath,
  .fsal_lookupjunction = FUSEFSAL_lookupJunction,
  .fsal_cleanobjectresources = COMMON_CleanObjectResources,
  .fsal_set_quota = COMMON_set_quota_noquota,
  .fsal_get_quota = COMMON_get_quota_noquota,
  .fsal_rcp = FUSEFSAL_rcp,
  .fsal_rcp_by_fileid = COMMON_rcp_by_fileid,
  .fsal_rename = FUSEFSAL_rename,
  .fsal_get_stats = FUSEFSAL_get_stats,
  .fsal_readlink = FUSEFSAL_readlink,
  .fsal_symlink = FUSEFSAL_symlink,
  .fsal_sync = FUSEFSAL_sync,
  .fsal_handlecmp = FUSEFSAL_handlecmp,
  .fsal_handle_to_hashindex = FUSEFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = FUSEFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL,
  .fsal_digesthandle = FUSEFSAL_DigestHandle,
  .fsal_expandhandle = FUSEFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = FUSEFSAL_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = FUSEFSAL_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter = FUSEFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = FUSEFSAL_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      FUSEFSAL_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      FUSEFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = FUSEFSAL_truncate,
  .fsal_unlink = FUSEFSAL_unlink,
  .fsal_getfsname = FUSEFSAL_GetFSName,
  .fsal_getxattrattrs = FUSEFSAL_GetXAttrAttrs,
  .fsal_listxattrs = FUSEFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = FUSEFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = FUSEFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = FUSEFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = FUSEFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = FUSEFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = FUSEFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = FUSEFSAL_RemoveXAttrByName,
  .fsal_getextattrs = COMMON_getextattrs_notsupp,
  .fsal_getfileno = FUSEFSAL_GetFileno
};

fsal_const_t fsal_fuse_consts = {
  .fsal_handle_t_size = sizeof(fusefsal_handle_t),
  .fsal_op_context_t_size = sizeof(fusefsal_op_context_t),
  .fsal_export_context_t_size = sizeof(fusefsal_export_context_t),
  .fsal_file_t_size = sizeof(fusefsal_file_t),
  .fsal_cookie_t_size = sizeof(fusefsal_cookie_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(fusefs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(fusefsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_fuse_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_fuse_consts;
}                               /* FSAL_GetConsts */
