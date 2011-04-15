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
 * \file    cache_content_add_entry.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:32 $
 * \version $Revision: 1.12 $
 * \brief   Management of the file content cache: adding a new entry.
 *
 * cache_content_add_entry.c : Management of the file content cache: adding a new entry.
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
#include "cache_content.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

/**
 *
 * cache_content_new_entry: adds an entry to the file content cache.
 *
 * Adds an entry to the file content cache.
 * This routine should be called only from the cache_inode layer. 
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is 
 * locked and will prevent from concurent accesses.
 *
 * @param pentry_inode [IN] entry in cache_inode layer for this file.
 * @param pspecdata [IN] pointer to the entry's specific data
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 */
cache_content_entry_t *cache_content_new_entry(cache_entry_t * pentry_inode,
                                               cache_content_spec_data_t * pspecdata,
                                               cache_content_client_t * pclient,
                                               cache_content_add_behaviour_t how,
                                               fsal_op_context_t * pcontext,
                                               cache_content_status_t * pstatus)
{
  cache_content_status_t status;
  cache_content_entry_t *pfc_pentry = NULL;
  int tmpfd;

  /* Set the return default to CACHE_CONTENT_SUCCESS */
  *pstatus = CACHE_CONTENT_SUCCESS;

  /* stat */
  pclient->stat.func_stats.nb_call[CACHE_CONTENT_NEW_ENTRY] += 1;

  if(pentry_inode == NULL)
    {
      *pstatus = CACHE_CONTENT_INVALID_ARGUMENT;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

      return NULL;
    }

  if(how != RENEW_ENTRY)
    {
      /* Get the entry from the preallocated pool */
      GetFromPool(pfc_pentry, &pclient->content_pool, cache_content_entry_t);

      if(pfc_pentry == NULL)
        {
          *pstatus = CACHE_CONTENT_MALLOC_ERROR;

          LogDebug(COMPONENT_CACHE_CONTENT, 
                            "cache_content_new_entry: can't allocate a new fc_entry from cache pool");

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

          return NULL;
        }
    }                           /* if( how != RENEW_ENTRY ) */
  else
    {
      /* When renewing a file content entry, pentry_content already exists in pentry_inode, just use it */
      pfc_pentry = (cache_content_entry_t *) (pentry_inode->object.file.pentry_content);
    }

  /* Set the path to the local files */
  if((status = cache_content_create_name(pfc_pentry->local_fs_entry.cache_path_index,
                                         CACHE_CONTENT_INDEX_FILE,
                                         pcontext,
                                         pentry_inode, pclient)) != CACHE_CONTENT_SUCCESS)
    {
      ReleaseToPool(pfc_pentry, &pclient->content_pool);

      *pstatus = CACHE_CONTENT_ENTRY_EXISTS;

      /* stat */
      pclient->stat.func_stats.nb_err_retryable[CACHE_CONTENT_NEW_ENTRY] += 1;

      LogEvent(COMPONENT_CACHE_CONTENT,
                        "cache_content_new_entry: entry's index pathname could not be created:%u", 
                        status );

      return NULL;
    }

  if((status = cache_content_create_name(pfc_pentry->local_fs_entry.cache_path_data,
                                         CACHE_CONTENT_DATA_FILE,
                                         pcontext,
                                         pentry_inode, pclient)) != CACHE_CONTENT_SUCCESS)
    {
      ReleaseToPool(pfc_pentry, &pclient->content_pool);

      *pstatus = CACHE_CONTENT_ENTRY_EXISTS;

      /* stat */
      pclient->stat.func_stats.nb_err_retryable[CACHE_CONTENT_NEW_ENTRY] += 1;

      LogEvent(COMPONENT_CACHE_CONTENT,
                        "cache_content_new_entry: entry's data  pathname could not be created");

      return NULL;
    }

  LogDebug(COMPONENT_CACHE_CONTENT,
                    "added file content cache entry: Data=%s Index=%s",
                    pfc_pentry->local_fs_entry.cache_path_data,
                    pfc_pentry->local_fs_entry.cache_path_index);

  /* Set the sync state */
  pfc_pentry->local_fs_entry.sync_state = JUST_CREATED;

  /* Set the internal_md */
  pfc_pentry->internal_md.read_time = 0;
  pfc_pentry->internal_md.mod_time = 0;
  pfc_pentry->internal_md.refresh_time = 0;
  pfc_pentry->internal_md.alloc_time = time(NULL);
  pfc_pentry->internal_md.last_flush_time = 0;
  pfc_pentry->internal_md.last_refresh_time = 0;
  pfc_pentry->internal_md.valid_state = STATE_OK;

  /* Set the local fd info */
  pfc_pentry->local_fs_entry.opened_file.local_fd = -1;
  pfc_pentry->local_fs_entry.opened_file.last_op = 0;

  /* Dump the inode entry to the index file */
  if(cache_inode_dump_content(pfc_pentry->local_fs_entry.cache_path_index, pentry_inode) 
     != CACHE_INODE_SUCCESS)
    {
      ReleaseToPool(pfc_pentry, &pclient->content_pool);

      *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;

      LogEvent(COMPONENT_CACHE_CONTENT,
                        "cache_content_new_entry: entry could not be dumped in file");

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

      return NULL;
    }

  /* Create the data file if entry is not recoverd (in this case, it already exists) */
  if(how == ADD_ENTRY || how == RENEW_ENTRY)
    {
      if((tmpfd = creat(pfc_pentry->local_fs_entry.cache_path_data, 0750)) == -1)
        {
          ReleaseToPool(pfc_pentry, &pclient->content_pool);

          *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;

          LogEvent(COMPONENT_CACHE_CONTENT,
                            "cache_content_new_entry: data cache file could not be created, errno=%d (%s)",
                            errno, strerror(errno));

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

          return NULL;
        }

      /* Close the new fd */
      close(tmpfd);

    }

  /* if( how == ADD_ENTRY || how == RENEW_ENTRY ) */
  /* Cache the data from FSAL if there are some */
  /* Add the entry to the related cache inode entry */
  pentry_inode->object.file.pentry_content = pfc_pentry;
  pfc_pentry->pentry_inode = pentry_inode;

  /* Data cache is considered as more pertinent as data below in case of crash recovery */
  if(how != RECOVER_ENTRY)
    {
      /* Get the file content from the FSAL, populate the data cache */
      if(pclient->flush_force_fsal == 0)
        cache_content_refresh(pfc_pentry, pclient, pcontext, DEFAULT_REFRESH, &status);
      else
        cache_content_refresh(pfc_pentry, pclient, pcontext, FORCE_FROM_FSAL, &status);

      if(status != CACHE_CONTENT_SUCCESS)
        {
          ReleaseToPool(pfc_pentry, &pclient->content_pool);

          *pstatus = status;

          LogEvent(COMPONENT_CACHE_CONTENT,
                            "cache_content_new_entry: data cache file could not read from FSAL, status=%u",
                            status);

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

          return NULL;
        }
    }
  /* if ( how != RECOVER_ENTRY ) */
  return pfc_pentry;
}                               /* cache_content_new_entry */
