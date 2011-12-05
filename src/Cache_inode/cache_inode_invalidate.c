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
  cache_entry_t * pentry = NULL ;

  cache_inode_fsal_data_t fsal_data; 
  hash_buffer_t key, value;
  int rc = 0 ; 

  if( pstatus == NULL || pattr == NULL || pclient == NULL || ht == NULL || pfsal_handle == NULL ) 
    return CACHE_INODE_INVALID_ARGUMENT ;

  /* Locate the entry in the cache */
  fsal_data.handle = *pfsal_handle ;
  fsal_data.cookie = 0;  /* No DIR_CONTINUE is managed here */

  /* Turn the input to a hash key */
  if(cache_inode_fsaldata_2_key(&key, &fsal_data, pclient))
   {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus ;
   }

  /* Search the cache for an entry with the related fsal_handle */
  if( ( rc = HashTable_Get(ht, &key, &value) ) == HASHTABLE_ERROR_NO_SUCH_KEY )
   {
      /* Entry is not cached */
      *pstatus = CACHE_INODE_NOT_FOUND ;
      return *pstatus ;
   }
  else if ( rc != HASHTABLE_SUCCESS )
   {
       LogCrit( COMPONENT_CACHE_INODE, "Unexpected error %u while calling HashTable_Get", rc ) ;
       
       *pstatus = CACHE_INODE_INVALID_ARGUMENT;
       return *pstatus ;
   }

  /* At this point, we are sure that an entry has been found */
  pentry = (cache_entry_t *)value.pdata;

  /* Never invalidate an entry that holds states */
  if( cache_inode_file_holds_state( pentry ) )
   {
       *pstatus = CACHE_INODE_STATE_CONFLICT ;
       return *pstatus ;
   }


  /* return attributes additionally (may be useful to be caller, at least for debugging purpose */
  cache_inode_get_attributes(pentry, pattr);

  /* pentry is lock, I call cache_inode_kill_entry with 'locked' flag set */
  P_w( &pentry->lock ) ; 
  return cache_inode_kill_entry( pentry, WT_LOCK, ht, pclient, pstatus ) ;
} /* cache_inode_invalidate */
