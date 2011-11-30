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

fsal_functions_t fsal_snmp_functions = {
  .fsal_access = SNMPFSAL_access,
  .fsal_getattrs = SNMPFSAL_getattrs,
  .fsal_setattrs = SNMPFSAL_setattrs,
  .fsal_buildexportcontext = SNMPFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = COMMON_CleanUpExportContext_noerror,
  .fsal_initclientcontext = SNMPFSAL_InitClientContext,
  .fsal_getclientcontext = COMMON_GetClientContext,
  .fsal_create = SNMPFSAL_create,
  .fsal_mkdir = SNMPFSAL_mkdir,
  .fsal_link = SNMPFSAL_link,
  .fsal_mknode = SNMPFSAL_mknode,
  .fsal_opendir = SNMPFSAL_opendir,
  .fsal_readdir = SNMPFSAL_readdir,
  .fsal_closedir = SNMPFSAL_closedir,
  .fsal_open_by_name = SNMPFSAL_open_by_name,
  .fsal_open = SNMPFSAL_open,
  .fsal_read = SNMPFSAL_read,
  .fsal_write = SNMPFSAL_write,
  .fsal_close = SNMPFSAL_close,
  .fsal_open_by_fileid = COMMON_open_by_fileid,
  .fsal_close_by_fileid = COMMON_close_by_fileid,
  .fsal_dynamic_fsinfo = SNMPFSAL_dynamic_fsinfo,
  .fsal_init = SNMPFSAL_Init,
  .fsal_terminate = COMMON_terminate_noerror,
  .fsal_test_access = SNMPFSAL_test_access,
  .fsal_setattr_access = COMMON_setattr_access_notsupp,
  .fsal_rename_access = COMMON_rename_access_notsupp,
  .fsal_create_access = COMMON_create_access,
  .fsal_unlink_access = COMMON_unlink_access,
  .fsal_link_access = COMMON_link_access,
  .fsal_merge_attrs = COMMON_merge_attrs,
  .fsal_lookup = SNMPFSAL_lookup,
  .fsal_lookuppath = SNMPFSAL_lookupPath,
  .fsal_lookupjunction = SNMPFSAL_lookupJunction,
  .fsal_cleanobjectresources = COMMON_CleanObjectResources,
  .fsal_set_quota = COMMON_set_quota_noquota,
  .fsal_get_quota = COMMON_get_quota_noquota,
  .fsal_rcp = SNMPFSAL_rcp,
  .fsal_rcp_by_fileid = COMMON_rcp_by_fileid,
  .fsal_rename = SNMPFSAL_rename,
  .fsal_get_stats = SNMPFSAL_get_stats,
  .fsal_readlink = SNMPFSAL_readlink,
  .fsal_symlink = SNMPFSAL_symlink,
  .fsal_sync = SNMPFSAL_sync,
  .fsal_handlecmp = SNMPFSAL_handlecmp,
  .fsal_handle_to_hashindex = SNMPFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = SNMPFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL, 
  .fsal_digesthandle = SNMPFSAL_DigestHandle,
  .fsal_expandhandle = SNMPFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = COMMON_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = COMMON_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter = SNMPFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = COMMON_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      COMMON_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      SNMPFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = SNMPFSAL_truncate,
  .fsal_unlink = SNMPFSAL_unlink,
  .fsal_getfsname = SNMPFSAL_GetFSName,
  .fsal_getxattrattrs = SNMPFSAL_GetXAttrAttrs,
  .fsal_listxattrs = SNMPFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = SNMPFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = SNMPFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = SNMPFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = SNMPFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = SNMPFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = SNMPFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = SNMPFSAL_RemoveXAttrByName,
  .fsal_getextattrs = COMMON_getextattrs_notsupp,
  .fsal_getfileno = SNMPFSAL_GetFileno
};

fsal_const_t fsal_snmp_consts = {
  .fsal_handle_t_size = sizeof(snmpfsal_handle_t),
  .fsal_op_context_t_size = sizeof(snmpfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(snmpfsal_export_context_t),
  .fsal_file_t_size = sizeof(snmpfsal_file_t),
  .fsal_cookie_t_size = sizeof(snmpfsal_cookie_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(snmpfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(snmpfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_snmp_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_snmp_consts;
}                               /* FSAL_GetConsts */
