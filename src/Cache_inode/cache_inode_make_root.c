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
 * \file    cache_inode_make_root.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.12 $
 * \brief   Insert in the cache an entry that is the root of the FS cached.
 *
 * cache_inode_make_root.c : Inserts in the cache an entry that is the root of the FS cached.
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

/**
 * cache_inode_make_root: Inserts the root of a FS in the cache.
 *
 * Inserts the root of a FS in the cache. This function will be called at junction traversal.
 *
 * @param pfsdata [IN] FSAL data for the root. 
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials. Unused here. 
 * @param pstatus [OUT] returned status.
 */

cache_entry_t *cache_inode_make_root(cache_inode_fsal_data_t * pfsdata,
                                     cache_inode_policy_t policy,
                                     hash_table_t * ht,
                                     cache_inode_client_t * pclient,
                                     fsal_op_context_t * pcontext,
                                     cache_inode_status_t * pstatus)
{
  cache_entry_t *pentry = NULL;
  cache_inode_parent_entry_t *next_parent_entry = NULL;

  /* sanity check */
  if(pstatus == NULL)
    return NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* BUGAZOMEU: gestion de junctions, : peut etre pas correct de faire pointer root sur lui meme */
  if((pentry = cache_inode_new_entry( pfsdata, 
                                      NULL, 
                                      DIRECTORY, 
                                      policy,
                                      NULL, 
                                      NULL, 
                                      ht,
                                      pclient, 
                                      pcontext, 
                                      FALSE,    /* This is a population, not a creation */
                                      pstatus)) != NULL)
    {
      GetFromPool(next_parent_entry, &pclient->pool_parent,
		  cache_inode_parent_entry_t);

      if(next_parent_entry == NULL)
        {
          *pstatus = CACHE_INODE_MALLOC_ERROR;
          pentry = NULL;
          return pentry;
        }

      pentry->parent_list = next_parent_entry;

      /* /!\ root is it own ".." */
      pentry->parent_list->parent = pentry;
      pentry->parent_list->next_parent = NULL;
    }

  return pentry;
}                               /* cache_inode_make_root */
