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
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:46:35 $
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

#include "stuff_alloc.h"
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
#include <errno.h>
#include <string.h>

/**
 *
 * cache_inode_init: Init the ressource necessary for the cache inode management.
 * 
 * Init the ressource necessary for the cache inode management.
 * 
 * @param param [IN] the parameter for this cache. 
 * @param pstatus [OUT] pointer to buffer used to store the status for the operation.
 *
 * @return 0 if operation failed, -1 if failed. 
 *
 */
int cache_content_init(cache_content_client_parameter_t param,
                       cache_content_status_t * pstatus)
{
  /* Try to create the cache directory */
  if(mkdir(param.cache_dir, 0750) != 0 && errno != EEXIST)
    {
      /* Cannot create the directory for caching data */
      fprintf(stderr, "Can't create cache dir = %s, error = ( %d, %s )\n",
              param.cache_dir, errno, strerror(errno));

      *pstatus = CACHE_CONTENT_INVALID_ARGUMENT;
      return -1;
    }

  /* Successfull exit */
  return 0;
}                               /* cache_content_init */

/**
 *
 * cache_content_init_dir: Init the directory for caching entries from a given export id. 
 *
 * @param param [IN] the parameter for this cache. 
 * @param export_id [IN] export id for the entries to be cached.
 * 
 * @return 0 if ok, -1 otherwise. Errno will be set with the error's value.
 *
 */
int cache_content_init_dir(cache_content_client_parameter_t param,
                           unsigned short export_id)
{
  char path_to_dir[MAXPATHLEN];

  snprintf(path_to_dir, MAXPATHLEN, "%s/export_id=%d", param.cache_dir, 0);

  if(mkdir(path_to_dir, 0750) != 0 && errno != EEXIST)
    {
      return -1;
    }

  return 0;
}                               /* cache_content_init_dir */

/**
 *
 * cache_content_client_init: Init the ressource necessary for the cache content client.
 *
 * Init the ressource necessary for the cache content client.
 *
 * @param param [IN] the parameter for this client
 * @param pstatus [OUT] pointer to buffer used to store the status for the operation.
 *
 * @return 0 if operation failed, -1 if failed.
 *
 */
int cache_content_client_init(cache_content_client_t * pclient,
                              cache_content_client_parameter_t param,
                              char *name)
{
  pclient->nb_prealloc = param.nb_prealloc_entry;
  pclient->flush_force_fsal = param.flush_force_fsal;
  pclient->max_fd_per_thread = param.max_fd_per_thread;
  pclient->retention = param.retention;
  pclient->use_cache = param.use_cache;
  strncpy(pclient->cache_dir, param.cache_dir, MAXPATHLEN);

  MakePool(&pclient->content_pool, pclient->nb_prealloc, cache_content_entry_t,
           NULL, NULL);
  NamePool(&pclient->content_pool, "Data Cache Client Pool for %s", name);
  if(!IsPoolPreallocated(&pclient->content_pool))
    {
      LogCrit(COMPONENT_CACHE_CONTENT, 
              "Error : can't init data_cache client entry pool");
      return 1;
    }

  /* Successfull exit */
  return 0;
}                               /* cache_content_init */
