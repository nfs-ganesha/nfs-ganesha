/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
 * ---------------------------------------
 */

/**
 * \file    cache_inode_lock.c
 * \author  $Author: deniel $
 * \date    $Date$
 * \version $Revision$
 * \brief   This file contains functions used in lock management.
 *
 * cache_inode_lock.c : This file contains functions used in lock management.
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
#include "log_functions.h"
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
#include <string.h>

static void cache_inode_lock_print(cache_entry_t * pentry)
{

  printf
      ("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Plein de chose a faire dans cache_inode_lock_print !!!!!!!!!!!!!!!!!!!!!!!!!\n");
  return;
#ifdef BUGAZOMEU
  if (pentry->internal_md.type == REGULAR_FILE)
    if (piter_state->state_type == CACHE_INODE_STATE_SHARE)
      printf
          ("       piter_lock=%p   next=%p prev=%p         offset=%llu      length=%llu\n",
           piter_state, piter_state->next, piter_state->prev,
           piter_state->data.lock.offset, piter_state->data.lock.length);
#endif                          /* BUGAZOMEU */
}                               /* cache_inode_lock_print */

/**
 *
 * cache_inode_lock_check_conflicting_range: checks for conflicts in lock ranges.
 *
 * Checks for conflicts in lock ranges.
 *
 * @param pentry    [INOUT] cache entry for which the lock is to be created
 * @param offset    [IN]    offset where the lock range start
 * @param length    [IN]    length for the lock range ( do not use CACHE_INODE_LOCK_OFFSET_EOF)
 * @param pfilelock [OUT]   pointer to the conflicting lock if a conflit is found, NULL if no conflict
 * @param pstatus   [OUT]   returned status.
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_lock_check_conflicting_range(cache_entry_t * pentry,
                                                              uint64_t offset,
                                                              uint64_t length,
                                                              nfs_lock_type4 lock_type,
                                                              cache_inode_status_t *
                                                              pstatus)
{
  if (pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* pentry should be there */
  if (pentry == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* pentry should be a file */
  if (pentry->internal_md.type != REGULAR_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* CACHE_INODE_LOCK_OFFSET_EOF should have been handled sooner, use "absolute" length */
  if (length == CACHE_INODE_LOCK_OFFSET_EOF)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  printf
      ("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Plein de chose a faire dans cache_inode_lock_print !!!!!!!!!!!!!!!!!!!!!!!!!\n");
  *pstatus = CACHE_INODE_INVALID_ARGUMENT;
  return *pstatus;

#ifdef BUGAZOMEU
  for (piter_state = pentry->object.file.state_v4; piter_state != NULL;
       piter_state = piter_state->next)
    {
      if (piter_state->state_type != CACHE_INODE_STATE_LOCK)
        continue;

      printf
          ("--- check_conflicting_range : offset=%llu length=%llu piter_state->offset=%llu piter_state->length=%llu lock_type=%u piter_state->lock_type=%u\n",
           offset, length, piter_state->data.lock.offset, piter_state->data.lock.length,
           lock_type, piter_state->data.lock.lock_type);

      /* Check for a "conflict on the left" */
      if ((offset <= piter_state->data.lock.offset) &&
          ((offset + length) >= piter_state->data.lock.offset))
        found = TRUE;

      /* Check for a "conflict on the right" */
      if ((offset >= piter_state->data.lock.offset) &&
          (offset <= (piter_state->data.lock.offset + piter_state->data.lock.length)))
        found = TRUE;

      if (found)
        {
          /* No conflict on read lock */
          if ((piter_state->data.lock.lock_type == CACHE_INODE_WRITE_LT)
              || (lock_type == CACHE_INODE_WRITE_LT))
            {
              *ppfilelock = piter_state;
              *pstatus = CACHE_INODE_LOCK_CONFLICT;
              return *pstatus;
            }
        }

      /* Locks are supposed to be ordered by their range. Stop search is lock is "on the right" 
       * of the right extremity of the requested lock */
      if ((offset + length) < piter_state->data.lock.offset)
        break;
    }

  /* If this line is reached, then no conflict were found */
  *ppfilelock = NULL;
  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
#endif                          /* BUGAZOMEU */
}                               /* cache_inode_lock_check_conflicting_range */

cache_inode_status_t cache_inode_lock_test(cache_entry_t * pentry,
                                           uint64_t offset,
                                           uint64_t length,
                                           nfs_lock_type4 lock_type,
                                           cache_inode_client_t * pclient,
                                           cache_inode_status_t * pstatus)
{
  /* stat */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_LOCKT] += 1;

  P_r(&pentry->lock);
  cache_inode_lock_check_conflicting_range(pentry, offset, length, lock_type, pstatus);
  V_r(&pentry->lock);

  if (*pstatus == CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOCKT] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_LOCK_CREATE] += 1;

  return *pstatus;
}                               /* cache_inode_lock_test */

/**
 *
 * cache_inode_lock_insert: insert a lock into the lock list.
 *
 * Inserts a lock into the lock list.
 *
 * @param pentry          [INOUT] cache entry for which the lock is to be created
 * @param pfilelock       [IN]    file lock to be inserted
 *
 * @return nothing (void function)
 *
 */

void cache_inode_lock_insert(cache_entry_t * pentry, cache_inode_state_t * pfilelock)
{

  printf
      ("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Plein de chose a faire dans cache_inode_lock_insert !!!!!!!!!!!!!!!!!!!!!!!!!\n");

  if (pentry == NULL)
    return;

  if (pentry->internal_md.type != REGULAR_FILE)
    return;

#ifdef BUGAZOMEU
  for (piter_state = pentry->object.file.state_v4; piter_state != NULL;
       piter_state_prev = piter_state, piter_state = piter_state->next)
    {
      if (pfilelock->data.lock.offset > piter_state->data.lock.offset)
        break;
    }

  /* At this line, piter_lock is the position before the new lock should be inserted */
  if (piter_state == NULL)
    {
      /* Lock is to be inserted at the end of list */
      pfilelock->next = NULL;
      pfilelock->prev = piter_state_prev;
      piter_state_prev->next = pfilelock;
    }
  else
    {
      pfilelock->next = piter_state->next;
      pfilelock->prev = piter_state;
      piter_state->next = pfilelock;
    }
#endif                          /* BUGAZOMEU */
}                               /* cache_inode_insert_lock */

/**
 *
 * cache_inode_lock_remove: removes a lock from the lock list.
 *
 * Remove a lock from the lock list.
 *
 * @param pfilelock       [IN]    file lock to be removed
 *
 * @return nothing (void function)
 *
 */
void cache_inode_lock_remove(cache_entry_t * pentry, cache_inode_client_t * pclient)
{

  printf
      ("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Plein de chose a faire dans cache_inode_lock_remove !!!!!!!!!!!!!!!!!!!!!!!!!\n");
  return;
}                               /* cache_inode_lock_remove */

/**
 *
 * cache_inode_lock_create: creates a new lock for a given entry.
 *
 * Creates a new lock for a given entry.
 *
 * @param pentry          [INOUT] cache entry for which the lock is to be created
 * @param offset          [IN]    offset where the lock range start
 * @param length          [IN]    length for the lock range (0xFFFFFFFFFFFFFFFF means "until the end of file")
 * @param clientid        [IN]    The client id for the lock owner 
 * @param client_inst_num [IN]    The client instance for the lock owner 
 * @param pclient         [INOUT] ressource allocated by the client for the nfs management.
 * @param pstatus         [OUT]   returned status.
 *
 * @return the same as *pstatus
 *
 */

cache_inode_status_t cache_inode_lock_create(cache_entry_t * pentry,
                                             uint64_t offset,
                                             uint64_t length,
                                             nfs_lock_type4 lock_type,
                                             open_owner4 * pstate_owner,
                                             unsigned int client_inst_num,
                                             cache_inode_client_t * pclient,
                                             cache_inode_status_t * pstatus)
{
  if (pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  printf
      ("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Plein de chose a faire dans cache_inode_lock_remove !!!!!!!!!!!!!!!!!!!!!!!!!\n");
  return CACHE_INODE_INVALID_ARGUMENT;

#ifdef BUGAZOMEU
  /* stat */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_LOCK_CREATE] += 1;

  /* pentry should be there */
  if (pentry == NULL)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOCK_CREATE] += 1;

      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* pentry should be a file */
  if (pentry->internal_md.type != REGULAR_FILE)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOCK_CREATE] += 1;

      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* Use absolute offset, manage CACHE_INODE_LOCK_OFFSET_EOF here */
  if (length == CACHE_INODE_LOCK_OFFSET_EOF)
    {
      if (offset > pentry->object.file.attributes.filesize)
        {
          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOCK_CREATE] += 1;

          *pstatus = CACHE_INODE_INVALID_ARGUMENT;
          return *pstatus;
        }

      abslength = pentry->object.file.attributes.filesize - offset;
    }
  else
    {
      abslength = length;
    }

  /* Lock the entry */
  P_w(&pentry->lock);

  /* Check if lock is conflicting with an existing one */
  if (cache_inode_lock_check_conflicting_range(pentry,
                                               offset,
                                               abslength,
                                               lock_type,
                                               ppfilelock,
                                               pstatus) != CACHE_INODE_SUCCESS)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOCK_CREATE] += 1;

      V_w(&pentry->lock);

      return *pstatus;
    }

  /* Get a new lock */
  GET_PREALLOC(pfilelock,
               pclient->pool_state_v4,
               pclient->nb_pre_state_v4, cache_inode_state_v4_t, next);

  if (pfilelock == NULL)
    {
      DisplayLogJdLevel(pclient->log_outputs, NIV_DEBUG,
                        "Can't allocate a new file lock from cache pool");
      *pstatus = CACHE_INODE_MALLOC_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOCK_CREATE] += 1;

      V_w(&pentry->lock);

      return *pstatus;
    }

  /* Fills in the lock structure */
  memset(pfilelock, 0, sizeof(cache_inode_state_v4_t));
  pfilelock->data.lock.offset = offset;
  pfilelock->data.lock.length = abslength;
  pfilelock->data.lock.lock_type = lock_type;
  pfilelock->state_type = CACHE_INODE_STATE_LOCK;
  pfilelock->clientid4 = clientid;
  pfilelock->client_inst_num = client_inst_num;
  pfilelock->seqid = 0;
  pfilelock->pentry = pentry;

  /* Insert the lock into the list */
  cache_inode_lock_insert(pentry, pfilelock);

  /* Successful operation */
  pclient->stat.func_stats.nb_success[CACHE_INODE_LOCK_CREATE] += 1;

  V_w(&pentry->lock);

  cache_inode_lock_print(pentry);

  *ppnewlock = pfilelock;
  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
#endif
}                               /* cache_inode_lock_create */
