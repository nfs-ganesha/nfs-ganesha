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
 * \file    cache_inode_readlink.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/12/05 09:02:36 $
 * \version $Revision: 1.16 $
 * \brief   Reads a symlink.
 *
 * cache_inode_readlink.c : Reads a symlink.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

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

cache_inode_status_t cache_inode_readlink(cache_entry_t * pentry, fsal_path_t * plink_content, hash_table_t * ht,       /* Unused, kept for protototype's homogeneity */
                                          cache_inode_client_t * pclient,
                                          fsal_op_context_t * pcontext,
                                          cache_inode_status_t * pstatus)
{
  fsal_status_t fsal_status;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_READLINK] += 1;

  /* Lock the entry */
  P_w(&pentry->lock);
  if(cache_inode_renew_entry(pentry, NULL, ht, pclient, pcontext, pstatus) !=
     CACHE_INODE_SUCCESS)
    {
      pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_READLINK] += 1;
      V_w(&pentry->lock);
      return *pstatus;
    }
  /* RW_Lock obtained as writer turns to reader */
  rw_lock_downgrade(&pentry->lock);

  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
    case DIR_BEGINNING:
    case DIR_CONTINUE:
    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    case FIFO_FILE:
      *pstatus = CACHE_INODE_BAD_TYPE;
      V_r(&pentry->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READLINK] += 1;

      return *pstatus;
      break;

    case SYMBOLIC_LINK:
      fsal_status = FSAL_pathcpy(plink_content, &(pentry->object.symlink.content));
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          V_r(&pentry->lock);

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              DisplayLog
                  ("cache_inode_readlink: Stale FSAL File Handle detected for pentry = %p",
                   pentry);

              if(cache_inode_kill_entry(pentry, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                DisplayLog("cache_inode_readlink: Could not kill entry %p, status = %u",
                           pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }
          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READLINK] += 1;

          return *pstatus;
        }

      break;
    }

  /* Release the entry */
  *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_GET, pclient);
  V_r(&pentry->lock);

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_READLINK] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_READLINK] += 1;

  return *pstatus;
}                               /* cache_inode_readlink */
