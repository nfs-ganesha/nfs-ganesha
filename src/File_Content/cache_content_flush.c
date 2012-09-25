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
 * \file    cache_content_init.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:33 $
 * \version $Revision: 1.8 $
 * \brief   Management of the file content cache: initialisation.
 *
 * cache_content.c : Management of the file content cache: initialisation.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

/**
 *
 * cache_content_flush: Flushes the content of a file in the local cache to the FSAL data. 
 *
 * Flushes the content of a file in the local cache to the FSAL data. 
 * This routine should be called only from the cache_inode layer. 
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is 
 * locked and will prevent from concurent accesses.
 *
 * @param pentry   [IN]  entry in file content layer whose content is to be flushed.
 * @param flushhow [IN]  should we delete the cached entry in local or not ? 
 * @param pclient  [IN]  ressource allocated by the client for the nfs management.
 * @pstatus        [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 */
cache_content_status_t cache_content_flush(cache_content_entry_t * pentry,
                                           cache_content_flush_behaviour_t flushhow,
                                           cache_content_client_t * pclient,
                                           fsal_op_context_t * pcontext,
                                           cache_content_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  fsal_path_t local_path;

  *pstatus = CACHE_CONTENT_SUCCESS;

  /* stat */
  pclient->stat.func_stats.nb_call[CACHE_CONTENT_FLUSH] += 1;

  /* Get the fsal handle */
  pfsal_handle = &pentry->pentry_inode->handle;

  /* Lock related Cache Inode pentry to avoid concurrency while read/write operation */
  PTHREAD_RWLOCK_WRLOCK(&pentry->pentry_inode->content_lock);

  /* Convert the path to FSAL path */
  fsal_status =
      FSAL_str2path(pentry->local_fs_entry.cache_path_data, MAXPATHLEN, &local_path);

  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = CACHE_CONTENT_FSAL_ERROR;

      /* Unlock related Cache Inode pentry */
      PTHREAD_RWLOCK_UNLOCK(&pentry->pentry_inode->content_lock);

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_FLUSH] += 1;

      return *pstatus;
    }
  /* Write the data from the local data file to the fs file */
  fsal_status = FSAL_rcp(pfsal_handle, pcontext, &local_path, FSAL_RCP_LOCAL_TO_FS);
  if(FSAL_IS_ERROR(fsal_status))
    {
      LogMajor(COMPONENT_CACHE_CONTENT,
                        "Error %d,%d from FSAL_rcp when flushing file", fsal_status.major,
                        fsal_status.minor);

      /* Unlock related Cache Inode pentry */
      PTHREAD_RWLOCK_UNLOCK(&pentry->pentry_inode->content_lock);

      *pstatus = CACHE_CONTENT_FSAL_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_FLUSH] += 1;

      return *pstatus;
    }

  /* To delete or not to delete ? That is the question ... */
  if(flushhow == CACHE_CONTENT_FLUSH_AND_DELETE)
    {
      /* Remove the index file from the data cache */
      if(unlink(pentry->local_fs_entry.cache_path_index))
        {
          /* Unlock related Cache Inode pentry */
          PTHREAD_RWLOCK_UNLOCK(&pentry->pentry_inode->content_lock);

          LogCrit(COMPONENT_CACHE_CONTENT, "Can't unlink flushed index %s, errno=%u(%s)",
                     pentry->local_fs_entry.cache_path_index, errno, strerror(errno));
          *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
          return *pstatus;
        }

      /* Remove the data file from the data cache */
      if(unlink(pentry->local_fs_entry.cache_path_data))
        {
          /* Unlock related Cache Inode pentry */
          PTHREAD_RWLOCK_UNLOCK(&pentry->pentry_inode->content_lock);

          LogCrit(COMPONENT_CACHE_CONTENT, "Can't unlink flushed index %s, errno=%u(%s)",
                     pentry->local_fs_entry.cache_path_data, errno, strerror(errno));
          *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
          return *pstatus;
        }
    }

  /* Unlock related Cache Inode pentry */
  PTHREAD_RWLOCK_UNLOCK(&pentry->pentry_inode->content_lock);

  /* Exit the function with no error */
  pclient->stat.func_stats.nb_success[CACHE_CONTENT_FLUSH] += 1;

  /* Update the internal metadata */
  pentry->internal_md.last_flush_time = time(NULL);
  pentry->local_fs_entry.sync_state = SYNC_OK;

  return *pstatus;
}                               /* cache_content_flush */

/**
 *
 * cache_content_refresh: Refreshes the whole content of a file in the local cache to the FSAL data. 
 *
 * Refreshes the whole content of a file in the local cache to the FSAL data.
 * This routine should be called only from the cache_inode layer. 
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is 
 * locked and will prevent from concurent accesses.
 *
* @param pentry [IN] entry in file content layer whose content is to be flushed.
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 * @todo: BUGAZOMEU: gestion de coherence de date a mettre en place
 */
cache_content_status_t cache_content_refresh(cache_content_entry_t * pentry,
                                             cache_content_client_t * pclient,
                                             fsal_op_context_t * pcontext,
                                             cache_content_refresh_how_t how,
                                             cache_content_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  cache_entry_t *pentry_inode = NULL;
  fsal_path_t local_path;
  struct stat buffstat;

  *pstatus = CACHE_CONTENT_SUCCESS;

  /* stat */
  pclient->stat.func_stats.nb_call[CACHE_CONTENT_REFRESH] += 1;

  /* Get the related cache inode entry */
  pentry_inode = (cache_entry_t *) pentry->pentry_inode;

  /* Get the fsal handle */
  pfsal_handle = &pentry_inode->handle;

  /* Convert the path to FSAL path */
  fsal_status =
      FSAL_str2path(pentry->local_fs_entry.cache_path_data, MAXPATHLEN, &local_path);

  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = CACHE_CONTENT_FSAL_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_FLUSH] += 1;

      return *pstatus;
    }

  /* Stat the data file to check for incoherency (this can occur in a crash recovery context) */
  if(stat(pentry->local_fs_entry.cache_path_data, &buffstat) == -1)
    {
      *pstatus = CACHE_CONTENT_FSAL_ERROR;

      LogMajor(COMPONENT_CACHE_CONTENT,
                        "cache_content_refresh: could'nt stat on %s, errno=%u(%s)",
                        pentry->local_fs_entry.cache_path_data, errno, strerror(errno));

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_FLUSH] += 1;

      return *pstatus;
    }

  if(how == FORCE_FROM_FSAL)
    LogFullDebug(COMPONENT_FSAL,"FORCE FROM FSAL");
  else
    LogFullDebug(COMPONENT_FSAL,"FORCE FROM FSAL INACTIVE");


  if((how != FORCE_FROM_FSAL)
     && (buffstat.st_mtime > (time_t) pentry_inode->attributes.mtime.seconds))
    {
      *pstatus = CACHE_CONTENT_SUCCESS;

      LogDebug(COMPONENT_CACHE_CONTENT,
                        "Entry %p is more recent in data cache, keeping it", pentry);
      pentry_inode->attributes.mtime.seconds = buffstat.st_mtime;
      pentry_inode->attributes.mtime.nseconds = 0;
      pentry_inode->attributes.atime.seconds = buffstat.st_atime;
      pentry_inode->attributes.atime.nseconds = 0;
      pentry_inode->attributes.ctime.seconds = buffstat.st_ctime;
      pentry_inode->attributes.ctime.nseconds = 0;
    }
  else
    {
      /* Write the data from the local data file to the fs file */
      fsal_status = FSAL_rcp(pfsal_handle, pcontext, &local_path,
                             FSAL_RCP_FS_TO_LOCAL);
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = CACHE_CONTENT_FSAL_ERROR;
          LogMajor(COMPONENT_CACHE_CONTENT,
                            "FSAL_rcp failed for %s: fsal_status.major=%u fsal_status.minor=%u",
                            pentry->local_fs_entry.cache_path_data, fsal_status.major,
                            fsal_status.minor);

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_REFRESH] += 1;

          return *pstatus;
        }

      /* Exit the function with no error */
      pclient->stat.func_stats.nb_success[CACHE_CONTENT_REFRESH] += 1;

      /* Update the internal metadata */
      pentry->internal_md.last_refresh_time = time(NULL);
      pentry->local_fs_entry.sync_state = SYNC_OK;

    }

  return *pstatus;
}                               /* cache_content_refresh */

cache_content_status_t cache_content_sync_all(cache_content_client_t * pclient,
                                              fsal_op_context_t * pcontext,
                                              cache_content_status_t * pstatus)
{
  *pstatus = CACHE_CONTENT_SUCCESS;
  return *pstatus;
}                               /* cache_content_sync_all */
