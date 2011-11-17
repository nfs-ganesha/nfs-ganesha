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
 * \file    cache_inode_lookupp.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.5 $
 * \brief   Perform lookup through the cache to get the parent entry for a directory.
 *
 * cache_inode_lookupp.c : Perform lookup through the cache to get the parent entry for a directory.
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
 *
 * cache_inode_lookupp_sw: looks up (and caches) the parent directory for a directory. A switches tells is mutex are use.
 * 
 * Looks up (and caches) the parent directory for a directory.
 *
 * @param pentry [IN] entry whose parent is to be obtained.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * @param use_mutex [IN] if TRUE mutex are use, not otherwise.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookupp_sw( cache_entry_t * pentry,
                                       hash_table_t * ht,
                                       cache_inode_client_t * pclient,
                                       fsal_op_context_t * pcontext,
                                       cache_inode_status_t * pstatus, int use_mutex)
{
  cache_entry_t *pentry_parent = NULL;
  fsal_status_t fsal_status;
  fsal_attrib_list_t object_attributes;
  cache_inode_fsal_data_t fsdata;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  (pclient->stat.nb_call_total)++;
  (pclient->stat.func_stats.nb_call[CACHE_INODE_LOOKUP])++;

  /* The entry should be a directory */
  if(use_mutex)
    P_r(&pentry->lock);
  if(pentry->internal_md.type != DIRECTORY)
    {
      if(use_mutex)
        V_r(&pentry->lock);
      *pstatus = CACHE_INODE_BAD_TYPE;

      /* stats */
      (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUPP])++;

      return NULL;
    }

  /* Renew the entry (to avoid having it being garbagged */
  if(cache_inode_renew_entry(pentry, NULL, ht, pclient, pcontext, pstatus) !=
     CACHE_INODE_SUCCESS)
    {
      (pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_GETATTR])++;
      return NULL;
    }

  /* Does the parent belongs to the cache ? */
  if(pentry->parent_list && pentry->parent_list->parent)
    {
      /* YES, the parent is cached, use the pentry that we have found */
      pentry_parent = pentry->parent_list->parent;
    }
  else
    {
      /* NO, the parent is not cached, query FSAL to get it and cache the result */
      object_attributes.asked_attributes = pclient->attrmask;
      fsal_status =
          FSAL_lookup(&pentry->object.dir.handle, (fsal_name_t *) & FSAL_DOT_DOT,
                      pcontext, &fsdata.handle, &object_attributes);

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          if(use_mutex)
            V_r(&pentry->lock);

          /* Stale File Handle to be detected and managed */
          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogDebug(COMPONENT_CACHE_INODE,
                       "cache_inode_lookupp: Stale FSAL FH detected for pentry %p",
                       pentry);

              if(cache_inode_kill_entry(pentry, NO_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_remove: Could not kill entry %p, status = %u",
                        pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          /* stats */
          (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUPP])++;

          return NULL;
        }

      /* Call cache_inode_get to populate the cache with the parent entry */
      fsdata.cookie = 0;

      if((pentry_parent = cache_inode_get_located( &fsdata,
                                                   pentry,
                                                   pentry->policy, /* same policy as son */
                                                   &object_attributes,
                                                   ht, 
                                                   pclient, 
                                                   pcontext, 
                                                   pstatus)) == NULL)
        {
          if(use_mutex)
            V_r(&pentry->lock);

          /* stats */
          (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUPP])++;

          return NULL;
        }
    }

  *pstatus = cache_inode_valid(pentry_parent, CACHE_INODE_OP_GET, pclient);
  if(use_mutex)
    V_r(&pentry->lock);

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    (pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_LOOKUPP])++;
  else
    (pclient->stat.func_stats.nb_success[CACHE_INODE_LOOKUPP])++;

  return pentry_parent;
}                               /* cache_inode_lookupp_sw */

/**
 *
 * cache_inode_lookupp: looks up (and caches) the parent directory for a directory.
 * 
 * Looks up (and caches) the parent directory for a directory.
 *
 * @param pentry [IN] entry whose parent is to be obtained.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookupp(cache_entry_t * pentry,
                                   hash_table_t * ht,
                                   cache_inode_client_t * pclient,
                                   fsal_op_context_t * pcontext,
                                   cache_inode_status_t * pstatus)
{
  return cache_inode_lookupp_sw(pentry, ht, pclient, pcontext, pstatus, TRUE);
}                               /* cache_inode_lookupp_sw */

/**
 *
 * cache_inode_lookupp_no_mutex: looks up (and caches) the parent directory for a directory. No mutex management
 * 
 * Looks up (and caches) the parent directory for a directory.
 *
 * @param pentry [IN] entry whose parent is to be obtained.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookupp_no_mutex(cache_entry_t * pentry,
                                            hash_table_t * ht,
                                            cache_inode_client_t * pclient,
                                            fsal_op_context_t * pcontext,
                                            cache_inode_status_t * pstatus)
{
  return cache_inode_lookupp_sw(pentry, ht, pclient, pcontext, pstatus, FALSE);
}                               /* cache_inode_lookupp_no_mutex */
