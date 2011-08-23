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

fsal_functions_t fsal_zfs_functions = {
  .fsal_access = ZFSFSAL_access,
  .fsal_getattrs = ZFSFSAL_getattrs,
  .fsal_setattrs = ZFSFSAL_setattrs,
  .fsal_buildexportcontext = ZFSFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = ZFSFSAL_CleanUpExportContext,
  .fsal_initclientcontext = NULL,
  .fsal_getclientcontext = NULL,
  .fsal_create = ZFSFSAL_create,
  .fsal_mkdir = ZFSFSAL_mkdir,
  .fsal_link = ZFSFSAL_link,
  .fsal_mknode = ZFSFSAL_mknode,
  .fsal_opendir = ZFSFSAL_opendir,
  .fsal_readdir = ZFSFSAL_readdir,
  .fsal_closedir = ZFSFSAL_closedir,
  .fsal_open_by_name = ZFSFSAL_open_by_name,
  .fsal_open = ZFSFSAL_open,
  .fsal_read = ZFSFSAL_read,
  .fsal_write = ZFSFSAL_write,
  .fsal_close = ZFSFSAL_close,
  .fsal_open_by_fileid = ZFSFSAL_open_by_fileid,
  .fsal_close_by_fileid = ZFSFSAL_close_by_fileid,
  .fsal_static_fsinfo = ZFSFSAL_static_fsinfo,
  .fsal_dynamic_fsinfo = ZFSFSAL_dynamic_fsinfo,
  .fsal_init = ZFSFSAL_Init,
  .fsal_terminate = ZFSFSAL_terminate,
  .fsal_test_access = ZFSFSAL_test_access,
  .fsal_setattr_access = ZFSFSAL_setattr_access,
  .fsal_rename_access = ZFSFSAL_rename_access,
  .fsal_create_access = ZFSFSAL_create_access,
  .fsal_unlink_access = ZFSFSAL_unlink_access,
  .fsal_link_access = ZFSFSAL_link_access,
  .fsal_merge_attrs = ZFSFSAL_merge_attrs,
  .fsal_lookup = ZFSFSAL_lookup,
  .fsal_lookuppath = ZFSFSAL_lookupPath,
  .fsal_lookupjunction = ZFSFSAL_lookupJunction,
  .fsal_lock = ZFSFSAL_lock,
  .fsal_changelock = ZFSFSAL_changelock,
  .fsal_unlock = ZFSFSAL_unlock,
  .fsal_getlock = ZFSFSAL_getlock,
  .fsal_cleanobjectresources = ZFSFSAL_CleanObjectResources,
  .fsal_set_quota = ZFSFSAL_set_quota,
  .fsal_get_quota = ZFSFSAL_get_quota,
  .fsal_rcp = ZFSFSAL_rcp,
  .fsal_rcp_by_fileid = ZFSFSAL_rcp_by_fileid,
  .fsal_rename = ZFSFSAL_rename,
  .fsal_get_stats = ZFSFSAL_get_stats,
  .fsal_readlink = ZFSFSAL_readlink,
  .fsal_symlink = ZFSFSAL_symlink,
  .fsal_handlecmp = ZFSFSAL_handlecmp,
  .fsal_handle_to_hashindex = ZFSFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = ZFSFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL, 
  .fsal_digesthandle = ZFSFSAL_DigestHandle,
  .fsal_expandhandle = ZFSFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = ZFSFSAL_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = ZFSFSAL_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter = ZFSFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = ZFSFSAL_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      ZFSFSAL_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      ZFSFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = ZFSFSAL_truncate,
  .fsal_unlink = ZFSFSAL_unlink,
  .fsal_sync = ZFSFSAL_sync,
  .fsal_getfsname = ZFSFSAL_GetFSName,
  .fsal_getxattrattrs = ZFSFSAL_GetXAttrAttrs,
  .fsal_listxattrs = ZFSFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = ZFSFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = ZFSFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = ZFSFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = ZFSFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = ZFSFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = ZFSFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = ZFSFSAL_RemoveXAttrByName,
  .fsal_getfileno = ZFSFSAL_GetFileno,
  .fsal_getextattrs = ZFSFSAL_getextattrs
};

fsal_const_t fsal_zfs_consts = {
  .fsal_handle_t_size = sizeof(zfsfsal_handle_t),
  .fsal_op_context_t_size = sizeof(zfsfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(zfsfsal_export_context_t),
  .fsal_file_t_size = sizeof(zfsfsal_file_t),
  .fsal_cookie_t_size = sizeof(zfsfsal_cookie_t),
  .fsal_lockdesc_t_size = sizeof(zfsfsal_lockdesc_t),
  .fsal_cred_t_size = sizeof(struct user_credentials),
  .fs_specific_initinfo_t_size = sizeof(zfsfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(zfsfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_zfs_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_zfs_consts;
}                               /* FSAL_GetConsts */
