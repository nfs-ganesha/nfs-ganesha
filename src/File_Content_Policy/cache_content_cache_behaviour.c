/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    cache_content_cache_behaviour.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/08/03 07:22:17 $
 * \version $Revision: 1.2 $
 * \brief   Management of the file content cache: choose if an entry in the metadata cache is to be cached or not.
 *
 * cache_content_cache_behaviour.c : Choose if an entry in the metadata cache is to be cached or not.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "cache_content_policy.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

/**
 *
 * cache_content_cache_behaviour: chooses if a file is to be cached in data cache or not, basec o the caching policy.
 *
 * Chooses if a file is to be cached in data cache or not, basec o the caching policy.
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is 
 * locked and will prevent from concurent accesses.
 *
 * @param pentry [IN] entry in file content layer whose content is to be flushed.
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_FULLY_CACHED if file is to be cached
 *
 */

cache_content_caching_type_t cache_content_cache_behaviour(cache_entry_t * pentry_inode,
                                                           cache_content_policy_data_t *
                                                           ppolicy_data,
                                                           cache_content_client_t *
                                                           pclient,
                                                           cache_content_status_t *
                                                           pstatus)
{
  *pstatus = CACHE_CONTENT_FULLY_CACHED;

  if(pentry_inode->internal_md.type != REGULAR_FILE)
    {
      *pstatus = CACHE_CONTENT_INVALID_ARGUMENT;
      return *pstatus;
    }

  if(ppolicy_data->UseMaxCacheSize)
    {
      if(pentry_inode->object.file.attributes.filesize > ppolicy_data->MaxCacheSize)
        *pstatus = CACHE_CONTENT_TOO_LARGE_FOR_CACHE;
    }

  return *pstatus;
}                               /* cache_content_cache_behaviour */
