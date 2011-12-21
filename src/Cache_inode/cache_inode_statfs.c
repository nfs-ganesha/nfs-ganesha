/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file    cache_inode_statfs.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:28 $
 * \version $Revision: 1.13 $
 * \brief   Get and eventually cache an entry.
 *
 * cache_inode_statfs.c : Get static and dynamic info on the cache inode layer. 
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"


cache_inode_status_t cache_inode_statfs(cache_entry_t * pentry,
                                        fsal_dynamicfsinfo_t * pdynamicinfo,
                                        fsal_op_context_t * pcontext,
                                        cache_inode_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle;
  fsal_status_t fsal_status;

  /* Sanity check */
  if(!pentry || !pcontext || !pdynamicinfo || !pstatus)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Default return value */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Get the handle for this entry */
  if((pfsal_handle = cache_inode_get_fsal_handle(pentry, pstatus)) == NULL)
    return *pstatus;

  /* Get FSAL to get dynamic info */
  if(FSAL_IS_ERROR
     ((fsal_status = FSAL_dynamic_fsinfo(pfsal_handle, pcontext, pdynamicinfo))))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      return *pstatus;
    }
  LogFullDebug(COMPONENT_CACHE_INODE,
      "-- cache_inode_statfs --> pdynamicinfo->total_bytes = %llu pdynamicinfo->free_bytes = %llu pdynamicinfo->avail_bytes = %llu",
       pdynamicinfo->total_bytes, pdynamicinfo->free_bytes, pdynamicinfo->avail_bytes);

  LogFullDebug(COMPONENT_CACHE_INODE,
      "-- cache_inode_statfs --> dynamicinfo.total_files = %llu dynamicinfo.free_files = %llu dynamicinfo.avail_files = %llu",
       pdynamicinfo->total_files, pdynamicinfo->free_files, pdynamicinfo->avail_files);
  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_get */
