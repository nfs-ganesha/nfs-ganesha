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
 * \file    cache_inode_release_data_cache.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:27 $
 * \version $Revision: 1.4 $
 * \brief   Associates a File Content cache entry to a pentry of type REGULAR FILE. 
 *
 * cache_inode_release_data_cache.c : Associates a File Content cache entry to a pentry of type REGULAR FILE. 
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

cache_inode_status_t cache_inode_release_data_cache(cache_entry_t * pentry,
                                                    hash_table_t * ht,
                                                    cache_inode_client_t * pclient,
                                                    fsal_op_context_t * pcontext,
                                                    cache_inode_status_t * pstatus)
{
  cache_content_status_t cache_content_status;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_RELEASE_DATA_CACHE] += 1;

  P_w(&pentry->lock);

  /* Operate only on a regular file */
  if(pentry->internal_md.type != REGULAR_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      V_w(&pentry->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RELEASE_DATA_CACHE] += 1;

      return *pstatus;
    }

  if(pentry->object.file.pentry_content == NULL)
    {
      /* The object is not cached */
      *pstatus = CACHE_INODE_CACHE_CONTENT_EMPTY;

      V_w(&pentry->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_RELEASE_DATA_CACHE] += 1;

      return *pstatus;
    }

  if(cache_content_release_entry
     ((cache_content_entry_t *) pentry->object.file.pentry_content,
      (cache_content_client_t *) pclient->pcontent_client,
      &cache_content_status) != CACHE_CONTENT_SUCCESS)
    {
      *pstatus = cache_content_error_convert(cache_content_status);
      V_w(&pentry->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RELEASE_DATA_CACHE] += 1;

      return *pstatus;
    }

  /* Detache the entry from the cache inode */
  pentry->object.file.pentry_content = NULL;

  V_w(&pentry->lock);
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RELEASE_DATA_CACHE] += 1;

  return *pstatus;
}                               /* cache_inode_release_data_cache */
