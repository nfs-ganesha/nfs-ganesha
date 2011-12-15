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
 * \file    cache_inode_truncate.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:28 $
 * \version $Revision: 1.19 $
 * \brief   Truncates a regular file.
 *
 * cache_inode_truncate.c : Truncates a regular file.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "fsal.h"
#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "cache_inode.h"
#include "cache_content.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_truncate_sw: truncates a regular file specified by its cache entry.
 * 
 * Truncates a regular file specified by its cache entry.
 *
 * @param pentry    [INOUT] entry pointer for the fs object to be truncated. 
 * @param length    [IN]    wanted length for the file. 
 * @param pattr     [OUT]   attrtibutes for the file after the operation. 
 * @param ht        [INOUT] hash table used for the cache. Unused in this call (kept for protototype's homogeneity). 
 * @param pclient   [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext     [IN]    FSAL credentials 
 * @param pstatus   [OUT]   returned status.
 * @param use_mutex [IN]    if TRUE, mutex management is done, not if equal to FALSE.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_truncate_sw(cache_entry_t * pentry,
                                             fsal_size_t length,
                                             fsal_attrib_list_t * pattr,
                                             hash_table_t * ht,
                                             cache_inode_client_t * pclient,
                                             fsal_op_context_t * pcontext,
                                             cache_inode_status_t * pstatus,
                                             int use_mutex)
{
  fsal_status_t fsal_status;
  cache_content_status_t cache_content_status;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_TRUNCATE] += 1;

  if(use_mutex)
    P_w(&pentry->lock);

  /* Only regular files can be truncated */
  if(pentry->internal_md.type != REGULAR_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      if(use_mutex)
        V_w(&pentry->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_TRUNCATE] += 1;

      return *pstatus;
    }

  /* Calls file content cache to operate on the cache */
  if(pentry->object.file.pentry_content != NULL)
    {
      if(cache_content_truncate(pentry->object.file.pentry_content,
                                length,
                                (cache_content_client_t *) pclient->pcontent_client,
                                &cache_content_status) != CACHE_CONTENT_SUCCESS)
        {
          *pstatus = cache_content_error_convert(cache_content_status);
          if(use_mutex)
            V_w(&pentry->lock);

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_TRUNCATE] += 1;

          return *pstatus;
        }

      /* Cache truncate succeeded, we must now update the size in the attributes */
      if((pentry->object.file.attributes.asked_attributes & FSAL_ATTR_SIZE) ||
         (pentry->object.file.attributes.asked_attributes & FSAL_ATTR_SPACEUSED))
        {
          pentry->object.file.attributes.filesize = length;
          pentry->object.file.attributes.spaceused = length;
        }

      /* Set the time stamp values too */
      cache_inode_set_time_current( &pentry->object.file.attributes.mtime ) ;
      pentry->object.file.attributes.ctime = pentry->object.file.attributes.mtime;
    }
  else
    {
      /* Call FSAL to actually truncate */
      pentry->object.file.attributes.asked_attributes = pclient->attrmask;
#ifdef _USE_MFSL
      fsal_status = MFSL_truncate(&pentry->mobject, pcontext, &pclient->mfsl_context, length, NULL,    
#ifdef _USE_PNFS
                                  &pentry->object.file.attributes, &pentry->object.file.pnfs_file);
#else
                                  &pentry->object.file.attributes, NULL);
#endif /* _USE_PNFS */
#else
      fsal_status = FSAL_truncate(&pentry->object.file.handle, pcontext, length, NULL,  /** @todo &pentry->object.file.open_fd.fd, *//* Used only with FSAL_PROXY */
                                  &pentry->object.file.attributes);
#endif /* _USE_MFSL */

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          if(use_mutex)
            V_w(&pentry->lock);

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_TRUNCATE] += 1;

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogEvent(COMPONENT_CACHE_INODE,
                       "cache_inode_truncate: Stale FSAL File Handle detected for pentry = %p",
                       pentry);

              if(cache_inode_kill_entry( pentry, NO_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_truncate: Could not kill entry %p, status = %u",
                        pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          return *pstatus;
        }
    }


  /* Validate the entry */
  *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_SET, pclient);

  /* Regular exit */
  if(use_mutex)
    V_w(&pentry->lock);

  /* Returns the attributes */
  *pattr = pentry->object.file.attributes;

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_TRUNCATE] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_TRUNCATE] += 1;

  return *pstatus;
}                               /* cache_inode_truncate_sw */

/**
 *
 * cache_inode_truncate_no_mutex: truncates a regular file specified by its cache entry (no mutex management).
 * 
 * Truncates a regular file specified by its cache entry.
 *
 * @param pentry    [INOUT] entry pointer for the fs object to be truncated. 
 * @param length    [IN]    wanted length for the file. 
 * @param pattr     [OUT]   attrtibutes for the file after the operation. 
 * @param ht        [INOUT] hash table used for the cache. Unused in this call (kept for protototype's homogeneity). 
 * @param pclient   [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext     [IN]    FSAL credentials 
 * @param pstatus   [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_truncate_no_mutex(cache_entry_t * pentry,
                                                   fsal_size_t length,
                                                   fsal_attrib_list_t * pattr,
                                                   hash_table_t * ht,
                                                   cache_inode_client_t * pclient,
                                                   fsal_op_context_t * pcontext,
                                                   cache_inode_status_t * pstatus)
{
  return cache_inode_truncate_sw(pentry,
                                 length, pattr, ht, pclient, pcontext, pstatus, FALSE);
}                               /* cache_inode_truncate_no_mutex */

/**
 *
 * cache_inode_truncate: truncates a regular file specified by its cache entry.
 * 
 * Truncates a regular file specified by its cache entry.
 *
 * @param pentry    [INOUT] entry pointer for the fs object to be truncated. 
 * @param length    [IN]    wanted length for the file. 
 * @param pattr     [OUT]   attrtibutes for the file after the operation. 
 * @param ht        [INOUT] hash table used for the cache. Unused in this call (kept for protototype's homogeneity). 
 * @param pclient   [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext     [IN]    FSAL credentials 
 * @param pstatus   [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_truncate(cache_entry_t * pentry,
                                          fsal_size_t length,
                                          fsal_attrib_list_t * pattr,
                                          hash_table_t * ht,
                                          cache_inode_client_t * pclient,
                                          fsal_op_context_t * pcontext,
                                          cache_inode_status_t * pstatus)
{
  return cache_inode_truncate_sw(pentry,
                                 length, pattr, ht, pclient, pcontext, pstatus, TRUE);
}                               /* cache_inode_truncate */
