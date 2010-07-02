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
#include "fsal_types.h"
#include "fsal_glue.h"
#include "fsal_internal.h"

fsal_functions_t fsal_xfs_functions = {
  .fsal_access = XFSFSAL_access                         ,
  .fsal_getattrs = XFSFSAL_getattrs                     ,
  .fsal_setattrs = XFSFSAL_setattrs                     ,
  .fsal_buildexportcontext = XFSFSAL_BuildExportContext ,
  .fsal_initclientcontext = XFSFSAL_InitClientContext   ,
  .fsal_getclientcontext = XFSFSAL_GetClientContext     ,
  .fsal_create = XFSFSAL_create                         ,
  .fsal_mkdir = XFSFSAL_mkdir                           ,
  .fsal_link = XFSFSAL_link                             ,
  .fsal_mknode = XFSFSAL_mknode                         ,
  .fsal_opendir = XFSFSAL_opendir                       ,
  .fsal_readdir = XFSFSAL_readdir                       ,
  .fsal_closedir = XFSFSAL_closedir                     ,
  .fsal_open_by_name = XFSFSAL_open_by_name             ,
  .fsal_open = XFSFSAL_open                             ,
  .fsal_read = XFSFSAL_read                             ,
  .fsal_write = XFSFSAL_write                           ,
  .fsal_close = XFSFSAL_close                           ,
  .fsal_open_by_fileid = XFSFSAL_open_by_fileid         ,
  .fsal_close_by_fileid = XFSFSAL_close_by_fileid       ,
  .fsal_static_fsinfo = XFSFSAL_static_fsinfo           ,
  .fsal_dynamic_fsinfo = XFSFSAL_dynamic_fsinfo         ,
  .fsal_init = XFSFSAL_Init                             ,
  .fsal_terminate = XFSFSAL_terminate                   ,
  .fsal_test_access = XFSFSAL_test_access               ,
  .fsal_setattr_access = XFSFSAL_setattr_access         ,
  .fsal_rename_access = XFSFSAL_rename_access           ,
  .fsal_create_access = XFSFSAL_create_access           ,
  .fsal_unlink_access = XFSFSAL_unlink_access           ,
  .fsal_link_access = XFSFSAL_link_access               ,
  .fsal_merge_attrs = XFSFSAL_merge_attrs               ,
  .fsal_lookup = XFSFSAL_lookup                         , 
  .fsal_lookuppath = XFSFSAL_lookupPath                 ,
  .fsal_lookupjunction = XFSFSAL_lookupJunction         ,       
  .fsal_lock = XFSFSAL_lock                             ,
  .fsal_changelock = XFSFSAL_changelock                 ,
  .fsal_unlock = XFSFSAL_unlock                         ,
  .fsal_getlock = XFSFSAL_getlock                       ,  
  .fsal_cleanobjectresources = XFSFSAL_CleanObjectResources
} ;

 
void compat( void )
{
  return ;
}
