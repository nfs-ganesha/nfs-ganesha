/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"

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
                                     hash_table_t * ht,
                                     cache_inode_client_t * pclient,
                                     fsal_op_context_t * pcontext,
                                     cache_inode_status_t * pstatus)
{
  cache_entry_t *pentry = NULL;

  /* sanity check */
  if(pstatus == NULL)
    return NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* BUGAZOMEU: gestion de junctions, : peut etre pas correct de faire pointer root sur lui meme */
  if((pentry = cache_inode_new_entry(pfsdata, NULL, DIR_BEGINNING, NULL, NULL, ht, pclient, pcontext, FALSE,    /* This is a population, not a creation */
                                     pstatus)) != NULL)
    {
      /* /!\ root is it own ".." */
      pentry->parent_list->parent = pentry;

    }

  return pentry;
}                               /* cache_inode_make_root */
