/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
 * ---------------------------------------
 */

/**
 * \file    cache_content_policy.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:22 $
 * \version $Revision: 1.4 $
 * \brief   Management of the cached content caching policy layer. 
 *
 * cache_content_policy.h : Management of the cached content layer policy
 *
 */

#ifndef _CACHE_CONTENT_POLICY_H
#define _CACHE_CONTENT_POLICY_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"

typedef enum cache_content_caching_type__
{ CACHE_CONTENT_NO_POLICY = 0,
  CACHE_CONTENT_NOT_CACHED = 1,
  CACHE_CONTENT_FULLY_CACHED = 2
} cache_content_caching_type_t;

typedef struct cache_content_policy_data__
{
  unsigned int UseMaxCacheSize;
  uint64_t MaxCacheSize;
} cache_content_policy_data_t;

cache_content_caching_type_t cache_content_cache_behaviour(cache_entry_t * pentry_inode,
                                                           cache_content_policy_data_t *
                                                           ppolicy_data,
                                                           cache_content_client_t *
                                                           pclient,
                                                           cache_content_status_t *
                                                           pstatus);

#endif                          /* _CACHE_CONTENT_POLICY_H */
