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

#ifdef _PNFS
#include "fsal_pnfs.h"
#endif /* _PNFS */

fsal_functions_t fsal_lustre_functions = {
  .fsal_access = LUSTREFSAL_access,
  .fsal_getattrs = LUSTREFSAL_getattrs,
  .fsal_setattrs = LUSTREFSAL_setattrs,
  .fsal_buildexportcontext = LUSTREFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = COMMON_CleanUpExportContext_noerror,
  .fsal_initclientcontext = COMMON_InitClientContext,
  .fsal_getclientcontext = COMMON_GetClientContext,
  .fsal_create = LUSTREFSAL_create,
  .fsal_mkdir = LUSTREFSAL_mkdir,
  .fsal_link = LUSTREFSAL_link,
  .fsal_mknode = LUSTREFSAL_mknode,
  .fsal_opendir = LUSTREFSAL_opendir,
  .fsal_readdir = LUSTREFSAL_readdir,
  .fsal_closedir = LUSTREFSAL_closedir,
  .fsal_open_by_name = LUSTREFSAL_open_by_name,
  .fsal_open = LUSTREFSAL_open,
  .fsal_read = LUSTREFSAL_read,
  .fsal_write = LUSTREFSAL_write,
  .fsal_commit = LUSTREFSAL_commit,
  .fsal_close = LUSTREFSAL_close,
  .fsal_open_by_fileid = COMMON_open_by_fileid,
  .fsal_close_by_fileid = COMMON_close_by_fileid,
  .fsal_dynamic_fsinfo = LUSTREFSAL_dynamic_fsinfo,
  .fsal_init = LUSTREFSAL_Init,
  .fsal_terminate = COMMON_terminate_noerror,
  .fsal_test_access = LUSTREFSAL_test_access,
  .fsal_setattr_access = COMMON_setattr_access_notsupp,
  .fsal_rename_access = COMMON_rename_access,
  .fsal_create_access = COMMON_create_access,
  .fsal_unlink_access = COMMON_unlink_access,
  .fsal_link_access = COMMON_link_access,
  .fsal_merge_attrs = COMMON_merge_attrs,
  .fsal_lock_op = LUSTREFSAL_lock_op,
  .fsal_lookup = LUSTREFSAL_lookup,
  .fsal_lookuppath = LUSTREFSAL_lookupPath,
  .fsal_lookupjunction = LUSTREFSAL_lookupJunction,
  .fsal_cleanobjectresources = COMMON_CleanObjectResources,
  .fsal_set_quota = COMMON_set_quota_noquota,
  .fsal_get_quota = COMMON_get_quota_noquota,
  .fsal_check_quota = COMMON_check_quota,
  .fsal_rcp = LUSTREFSAL_rcp,
  .fsal_rename = LUSTREFSAL_rename,
  .fsal_get_stats = LUSTREFSAL_get_stats,
  .fsal_readlink = LUSTREFSAL_readlink,
  .fsal_symlink = LUSTREFSAL_symlink,
  .fsal_handlecmp = LUSTREFSAL_handlecmp,
  .fsal_handle_to_hashindex = LUSTREFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = LUSTREFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL, 
  .fsal_digesthandle = LUSTREFSAL_DigestHandle,
  .fsal_expandhandle = LUSTREFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = COMMON_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = COMMON_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter =
      LUSTREFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = COMMON_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      COMMON_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      LUSTREFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = LUSTREFSAL_truncate,
  .fsal_unlink = LUSTREFSAL_unlink,
  .fsal_getfsname = LUSTREFSAL_GetFSName,
  .fsal_getxattrattrs = LUSTREFSAL_GetXAttrAttrs,
  .fsal_listxattrs = LUSTREFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = LUSTREFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = LUSTREFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = LUSTREFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = LUSTREFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = LUSTREFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = LUSTREFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = LUSTREFSAL_RemoveXAttrByName,
  .fsal_getextattrs = COMMON_getextattrs_notsupp,
  .fsal_getfileno = LUSTREFSAL_GetFileno,
  .fsal_share_op = COMMON_share_op_notsupp
};

fsal_const_t fsal_lustre_consts = {
  .fsal_handle_t_size = sizeof(lustrefsal_handle_t),
  .fsal_op_context_t_size = sizeof(lustrefsal_op_context_t),
  .fsal_export_context_t_size = sizeof(lustrefsal_export_context_t),
  .fsal_file_t_size = sizeof(lustrefsal_file_t),
  .fsal_cookie_t_size = sizeof(lustrefsal_cookie_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(lustrefs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(lustrefsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_lustre_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_lustre_consts;
}                               /* FSAL_GetConsts */

#ifdef _PNFS_MDS
fsal_mdsfunctions_t fsal_lustre_mdsfunctions = {
  .layoutget = LUSTREFSAL_layoutget,
  .layoutreturn = LUSTREFSAL_layoutreturn,
  .layoutcommit = LUSTREFSAL_layoutcommit,
  .getdeviceinfo = LUSTREFSAL_getdeviceinfo,
  .getdevicelist = LUSTREFSAL_getdevicelist
};

fsal_mdsfunctions_t FSAL_GetMDSFunctions(void)
{
  return fsal_lustre_mdsfunctions;
}
#endif /* _PNFS_MDS */

#ifdef _PNFS_DS
fsal_dsfunctions_t fsal_lustre_dsfunctions = {
  .DS_read = LUSTREFSAL_DS_read,
  .DS_write = LUSTREFSAL_DS_write,
  .DS_commit = LUSTREFSAL_DS_commit
};

fsal_dsfunctions_t FSAL_GetDSFunctions(void)
{
  return fsal_lustre_dsfunctions;
}
#endif /* _PNFS_DS */
