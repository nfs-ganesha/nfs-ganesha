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

#include <string.h>
#include "fsal.h"
#include "fsal_glue.h"
#include "fsal_internal.h"
#include "FSAL/common_methods.h"

fsal_functions_t fsal_xfs_functions = {
  .fsal_access = POSIXFSAL_access,
  .fsal_getattrs = POSIXFSAL_getattrs,
  .fsal_setattrs = POSIXFSAL_setattrs,
  .fsal_buildexportcontext = POSIXFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = COMMON_CleanUpExportContext_noerror,
  .fsal_initclientcontext = POSIXFSAL_InitClientContext,
  .fsal_getclientcontext = COMMON_GetClientContext,
  .fsal_create = POSIXFSAL_create,
  .fsal_mkdir = POSIXFSAL_mkdir,
  .fsal_link = POSIXFSAL_link,
  .fsal_mknode = POSIXFSAL_mknode,
  .fsal_opendir = POSIXFSAL_opendir,
  .fsal_readdir = POSIXFSAL_readdir,
  .fsal_closedir = POSIXFSAL_closedir,
  .fsal_open_by_name = POSIXFSAL_open_by_name,
  .fsal_open = POSIXFSAL_open,
  .fsal_read = POSIXFSAL_read,
  .fsal_write = POSIXFSAL_write,
  .fsal_sync = POSIXFSAL_sync,
  .fsal_close = POSIXFSAL_close,
  .fsal_open_by_fileid = COMMON_open_by_fileid,
  .fsal_close_by_fileid = COMMON_close_by_fileid,
  .fsal_dynamic_fsinfo = POSIXFSAL_dynamic_fsinfo,
  .fsal_init = POSIXFSAL_Init,
  .fsal_terminate = COMMON_terminate_noerror,
  .fsal_test_access = POSIXFSAL_test_access,
  .fsal_setattr_access = COMMON_setattr_access_notsupp,
  .fsal_rename_access = COMMON_rename_access,
  .fsal_create_access = COMMON_create_access,
  .fsal_unlink_access = COMMON_unlink_access,
  .fsal_link_access = COMMON_link_access,
  .fsal_merge_attrs = COMMON_merge_attrs,
  .fsal_lookup = POSIXFSAL_lookup,
  .fsal_lookuppath = POSIXFSAL_lookupPath,
  .fsal_lookupjunction = POSIXFSAL_lookupJunction,
  .fsal_cleanobjectresources = COMMON_CleanObjectResources,
  .fsal_set_quota = POSIXFSAL_set_quota,
  .fsal_get_quota = POSIXFSAL_get_quota,
  .fsal_rcp = POSIXFSAL_rcp,
  .fsal_rcp_by_fileid = COMMON_rcp_by_fileid,
  .fsal_rename = POSIXFSAL_rename,
  .fsal_get_stats = POSIXFSAL_get_stats,
  .fsal_readlink = POSIXFSAL_readlink,
  .fsal_symlink = POSIXFSAL_symlink,
  .fsal_handlecmp = POSIXFSAL_handlecmp,
  .fsal_handle_to_hashindex = POSIXFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = POSIXFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL, 
  .fsal_digesthandle = POSIXFSAL_DigestHandle,
  .fsal_expandhandle = POSIXFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = POSIXFSAL_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter =
      POSIXFSAL_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter =
      POSIXFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf =
      POSIXFSAL_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      POSIXFSAL_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      POSIXFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = POSIXFSAL_truncate,
  .fsal_unlink = POSIXFSAL_unlink,
  .fsal_getfsname = POSIXFSAL_GetFSName,
  .fsal_getxattrattrs = POSIXFSAL_GetXAttrAttrs,
  .fsal_listxattrs = POSIXFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = POSIXFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = POSIXFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = POSIXFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = POSIXFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = POSIXFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = POSIXFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = POSIXFSAL_RemoveXAttrByName,
  .fsal_getextattrs = COMMON_getextattrs_notsupp,
  .fsal_getfileno = POSIXFSAL_GetFileno
};

fsal_const_t fsal_xfs_consts = {
  .fsal_handle_t_size = sizeof(posixfsal_handle_t),
  .fsal_op_context_t_size = sizeof(posixfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(posixfsal_export_context_t),
  .fsal_file_t_size = sizeof(posixfsal_file_t),
  .fsal_cookie_t_size = sizeof(posixfsal_cookie_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(posixfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(posixfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_xfs_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_xfs_consts;
}                               /* FSAL_GetConsts */
