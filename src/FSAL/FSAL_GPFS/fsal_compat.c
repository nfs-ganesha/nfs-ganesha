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

fsal_functions_t fsal_gpfs_functions = {
  .fsal_access = GPFSFSAL_access,
  .fsal_getattrs = GPFSFSAL_getattrs,
  .fsal_getattrs_descriptor = GPFSFSAL_getattrs_descriptor,
  .fsal_setattrs = GPFSFSAL_setattrs,
  .fsal_buildexportcontext = GPFSFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = GPFSFSAL_CleanUpExportContext,
  .fsal_initclientcontext = COMMON_InitClientContext,
  .fsal_getclientcontext = COMMON_GetClientContext,
  .fsal_create = GPFSFSAL_create,
  .fsal_mkdir = GPFSFSAL_mkdir,
  .fsal_link = GPFSFSAL_link,
  .fsal_mknode = GPFSFSAL_mknode,
  .fsal_opendir = GPFSFSAL_opendir,
  .fsal_readdir = GPFSFSAL_readdir,
  .fsal_closedir = GPFSFSAL_closedir,
  .fsal_open_by_name = GPFSFSAL_open_by_name,
  .fsal_open = GPFSFSAL_open,
  .fsal_read = GPFSFSAL_read,
  .fsal_write = GPFSFSAL_write,
  .fsal_sync = GPFSFSAL_sync,
  .fsal_close = GPFSFSAL_close,
  .fsal_open_by_fileid = COMMON_open_by_fileid,
  .fsal_close_by_fileid = COMMON_close_by_fileid,
  .fsal_dynamic_fsinfo = GPFSFSAL_dynamic_fsinfo,
  .fsal_init = GPFSFSAL_Init,
  .fsal_terminate = COMMON_terminate_noerror,
  .fsal_test_access = GPFSFSAL_test_access,
  .fsal_setattr_access = COMMON_setattr_access_notsupp,
  .fsal_rename_access = COMMON_rename_access,
  .fsal_create_access = COMMON_create_access,
  .fsal_unlink_access = COMMON_unlink_access,
  .fsal_link_access = COMMON_link_access,
  .fsal_merge_attrs = COMMON_merge_attrs,
  .fsal_lookup = GPFSFSAL_lookup,
  .fsal_lookuppath = GPFSFSAL_lookupPath,
  .fsal_lookupjunction = GPFSFSAL_lookupJunction,
  .fsal_lock_op = GPFSFSAL_lock_op,
  .fsal_cleanobjectresources = COMMON_CleanObjectResources,
  .fsal_set_quota = COMMON_set_quota_noquota,
  .fsal_get_quota = COMMON_get_quota_noquota,
  .fsal_rcp = GPFSFSAL_rcp,
  .fsal_rcp_by_fileid = COMMON_rcp_by_fileid,
  .fsal_rename = GPFSFSAL_rename,
  .fsal_get_stats = GPFSFSAL_get_stats,
  .fsal_readlink = GPFSFSAL_readlink,
  .fsal_symlink = GPFSFSAL_symlink,
  .fsal_handlecmp = GPFSFSAL_handlecmp,
  .fsal_handle_to_hashindex = GPFSFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = GPFSFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL, 
  .fsal_digesthandle = GPFSFSAL_DigestHandle,
  .fsal_expandhandle = GPFSFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = GPFSFSAL_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = GPFSFSAL_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter = GPFSFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = GPFSFSAL_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      GPFSFSAL_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      GPFSFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = GPFSFSAL_truncate,
  .fsal_unlink = GPFSFSAL_unlink,
  .fsal_getfsname = GPFSFSAL_GetFSName,
  .fsal_getxattrattrs = GPFSFSAL_GetXAttrAttrs,
  .fsal_listxattrs = GPFSFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = GPFSFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = GPFSFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = GPFSFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = GPFSFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = GPFSFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = GPFSFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = GPFSFSAL_RemoveXAttrByName,
  .fsal_getextattrs = COMMON_getextattrs,
  .fsal_getfileno = GPFSFSAL_GetFileno,
#ifdef _USE_FSAL_UP
  .fsal_up_init = GPFSFSAL_UP_Init,
  .fsal_up_addfilter = GPFSFSAL_UP_AddFilter,
  .fsal_up_getevents = GPFSFSAL_UP_GetEvents
#endif /* _USE_FSAL_UP */
};

fsal_const_t fsal_gpfs_consts = {
  .fsal_handle_t_size = sizeof(gpfsfsal_handle_t),
  .fsal_op_context_t_size = sizeof(gpfsfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(gpfsfsal_export_context_t),
  .fsal_file_t_size = sizeof(gpfsfsal_file_t),
  .fsal_cookie_t_size = sizeof(gpfsfsal_cookie_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(gpfsfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(gpfsfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_gpfs_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_gpfs_consts;
}                               /* FSAL_GetConsts */
