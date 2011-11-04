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
 * \file    cache_inode_invalidate.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/16 08:22:29 $
 * \version $Revision: 1.13 $
 * \brief   Renews an entry in the cache inode.
 *
 * cache_inode_invalidate.c : Renews an entry in the cache inode.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * cache_inode_invalidate: invalidates an entry in the cache
 *
 * This function invalidates the related cache entry correponding to a FSAL handle. It is designed to be called as a FSAL upcall is triggered.
 *
 * @param pfsal_handle [IN] FSAL handle for the entry to be invalidated
 * @param pattr [OUT] the attributes for the entry (if found) in the cache just before it was invalidated
 * @param pclient [INOUT] Cache Inode client (useful for having pools in which entry releasing invalidated records)
 * @param pstatus [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_INVALID_ARGUMENT bad parameter(s) as input \n
 * @return CACHE_INODE_NOT_FOUND if entry is not cached \n
 * @return CACHE_INODE_STATE_CONFLICT if invalidating this entry would result is state conflict \n
 * @return CACHE_INODE_INCONSISTENT_ENTRY if entry is not consistent \n
 * @return Other errors shows a FSAL error.
 *
 */
cache_inode_status_t cache_inode_invalidate( fsal_handle_t        * pfsal_handle,
                                             fsal_attrib_list_t   * pattr,
                                             hash_table_t         * ht,
                                             cache_inode_client_t * pclient,
                                             cache_inode_status_t * pstatus)
{
  if( *pstatus == NULL ) 
    return CACHE_INODE_INVALID_ARGUMENT ;

  return *pstatus;
}                               /* cache_inode_invalidate */
