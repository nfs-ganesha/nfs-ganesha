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
#include "log_macros.h"
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

typedef struct cache_lock_entry_t
{
  struct cache_lock_entry_t * cle_next;
  struct cache_lock_entry_t * cle_prev;
  cache_blocking_t            cle_blocked;
  cache_lock_owner_type_t     cle_type;
  cache_inode_nlm_owner_t   * cle_owner_nlm;
  cache_inode_open_owner_t  * cle_owner_nfsv4;
  cache_lock_desc_t           cle_lock;
  void                      * cle_pcookie;
  int                         cle_cookie_size;
  granted_callback_t          cle_granted_callback;
} cache_lock_entry_t;

cache_inode_status_t cache_inode_test(cache_entry_t        * pentry,
                                      cache_lock_owner_t   * powner,
                                      cache_lock_desc_t    * lock,
                                      cache_lock_owner_t   * holder,   /* owner that holds conflicting lock */
                                      cache_lock_desc_t    * conflict, /* description of conflicting lock */
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t    * pcontext,
                                      cache_inode_status_t * pstatus)
{
  *pstatus = CACHE_INODE_LOCK_CONFLICT;
  return *pstatus;
}

cache_inode_status_t cache_inode_lock(cache_entry_t        * pentry,
                                      void                 * pcookie,
                                      int                    cookie_size,
                                      cache_blocking_t       blocking,
                                      granted_callback_t     granted_callback,
                                      bool_t                 reclaim,
                                      cache_lock_owner_t   * powner,
                                      cache_lock_desc_t    * lock,
                                      cache_lock_owner_t   * holder,   /* owner that holds conflicting lock */
                                      cache_lock_desc_t    * conflict, /* description of conflicting lock */
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t    * pcontext,
                                      cache_inode_status_t * pstatus)
{
  cache_lock_entry_t *new_lock = (void *)Mem_Alloc_Label(sizeof(*new_lock), "cache_lock_entry_t");
  *pstatus = CACHE_INODE_LOCK_CONFLICT;
  return *pstatus;
}

cache_inode_status_t cache_inode_unlock(cache_entry_t        * pentry,
                                        void                 * pcookie,
                                        int                    cookie_size,
                                        cache_lock_owner_t   * powner,
                                        cache_lock_desc_t    * lock,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t    * pcontext,
                                        cache_inode_status_t * pstatus)
{
  *pstatus = CACHE_INODE_NOT_SUPPORTED;
  return *pstatus;
}

cache_inode_status_t cache_inode_cancel(cache_entry_t        * pentry,
                                        cache_lock_owner_t   * powner,
                                        void                 * pcookie,
                                        int                    cookie_size,
                                        cache_lock_desc_t    * lock,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t    * pcontext,
                                        cache_inode_status_t * pstatus)
{
  *pstatus = CACHE_INODE_NOT_SUPPORTED;
  return *pstatus;
}

cache_inode_status_t cache_inode_notify(cache_entry_t        * pentry,
                                        cache_lock_owner_t   * powner,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t    * pcontext,
                                        cache_inode_status_t * pstatus)
{
  *pstatus = CACHE_INODE_NOT_SUPPORTED;
  return *pstatus;
}

#ifdef BUGAZOMEU
static void cache_inode_lock_print(cache_entry_t * pentry)
{

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "!!! Plein de chose a faire dans cache_inode_lock_print !!!!!!");
    return;
    if((pentry->internal_md.type == REGULAR_FILE) &&
       (piter_state->state_type == CACHE_INODE_STATE_SHARE))
            LogFullDebug(COMPONENT_CACHE_INODE,
                         "piter_lock=%p next=%p prev=%p offset=%llu length=%llu",
                         piter_state, piter_state->next, piter_state->prev,
                         piter_state->data.lock.offset, piter_state->data.lock.length);
}
#endif

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
cache_inode_status_t
cache_inode_lock_check_conflicting_range(cache_entry_t * pentry,
                                         uint64_t offset,
                                         uint64_t length,
                                         nfs_lock_type4 lock_type,
                                         cache_inode_status_t *
                                         pstatus)
{
    if(pstatus == NULL)
        return CACHE_INODE_INVALID_ARGUMENT;

    /* pentry should be there */
    if(pentry == NULL)
        {
            *pstatus = CACHE_INODE_INVALID_ARGUMENT;
            return *pstatus;
        }

    /* pentry should be a file */
    if(pentry->internal_md.type != REGULAR_FILE)
        {
            *pstatus = CACHE_INODE_BAD_TYPE;
            return *pstatus;
        }
    /*
     * CACHE_INODE_LOCK_OFFSET_EOF should have been handled
     * sooner, use "absolute" length
     */
    if(length == CACHE_INODE_LOCK_OFFSET_EOF)
        {
            *pstatus = CACHE_INODE_INVALID_ARGUMENT;
            return *pstatus;
        }

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "!!!!! Plein de chose a faire dans cache_inode_lock_print !!!!!");
    *pstatus = CACHE_INODE_INVALID_ARGUMENT;
    return *pstatus;

#ifdef BUGAZOMEU
    for(piter_state = pentry->object.file.state_v4; piter_state != NULL;
        piter_state = piter_state->next)
        {
            if(piter_state->state_type != CACHE_INODE_STATE_LOCK)
                continue;

            LogFullDebug(COMPONENT_CACHE_INODE,
                         "--- check_conflicting_range : offset=%llu length=%llu "
                         "piter_state->offset=%llu piter_state->length=%llu "
                         "lock_type=%u piter_state->lock_type=%u",
                         offset, length, piter_state->data.lock.offset,
                         piter_state->data.lock.length,
                         lock_type, piter_state->data.lock.lock_type);

            /* Check for a "conflict on the left" */
            if((offset <= piter_state->data.lock.offset) &&
               ((offset + length) >= piter_state->data.lock.offset))
                found = TRUE;

            /* Check for a "conflict on the right" */
            if((offset >= piter_state->data.lock.offset) &&
               (offset <= (piter_state->data.lock.offset +
                           piter_state->data.lock.length)))
                found = TRUE;

            if(found)
                {
                    /* No conflict on read lock */
                    if((piter_state->data.lock.lock_type == CACHE_INODE_WRITE_LT) ||
                       (lock_type == CACHE_INODE_WRITE_LT))
                        {
                            *ppfilelock = piter_state;
                            *pstatus = CACHE_INODE_LOCK_CONFLICT;
                            return *pstatus;
                        }
                }

            /*
             * Locks are supposed to be ordered by their range.
             * Stop search is lock is "on the right"
             * of the right extremity of the requested lock
             */
            if((offset + length) < piter_state->data.lock.offset)
                break;
        }

    /* If this line is reached, then no conflict were found */
    *ppfilelock = NULL;
    *pstatus = CACHE_INODE_SUCCESS;
    return *pstatus;
#endif    /* BUGAZOMEU */
}

cache_inode_status_t
cache_inode_lock_test(cache_entry_t * pentry,
                      uint64_t offset,
                      uint64_t length,
                      nfs_lock_type4 lock_type,
                      cache_inode_client_t * pclient,
                      cache_inode_status_t * pstatus)
{
    /* stat */
    pclient->stat.nb_call_total += 1;
    inc_func_call(pclient, CACHE_INODE_LOCKT);

    P_r(&pentry->lock);
    cache_inode_lock_check_conflicting_range(pentry, offset,
                                             length, lock_type,
                                             pstatus);
    V_r(&pentry->lock);

    if(*pstatus == CACHE_INODE_SUCCESS)
        inc_func_err_unrecover(pclient, CACHE_INODE_LOCKT);
    else
        inc_func_success(pclient, CACHE_INODE_LOCKT);
    return *pstatus;
}

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

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "!!!!! Plein de chose a faire dans cache_inode_lock_insert !!!!!!");

    if(pentry == NULL)
        return;

    if(pentry->internal_md.type != REGULAR_FILE)
        return;

#ifdef BUGAZOMEU
    for(piter_state = pentry->object.file.state_v4; piter_state != NULL;
        piter_state_prev = piter_state, piter_state = piter_state->next)
        {
            if(pfilelock->data.lock.offset > piter_state->data.lock.offset)
                break;
        }

    /* At this line, piter_lock is the position before the new lock should be inserted */
    if(piter_state == NULL)
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

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "!!!!!!!! Plein de chose a faire dans cache_inode_lock_remove !!!!");
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
    if(pstatus == NULL)
        return CACHE_INODE_INVALID_ARGUMENT;

    /* Set the return default to CACHE_INODE_SUCCESS */
    *pstatus = CACHE_INODE_SUCCESS;

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "!!!!!!! Plein de chose a faire dans cache_inode_lock_remove !!!!!!!");
    return CACHE_INODE_INVALID_ARGUMENT;

#ifdef BUGAZOMEU
    /* stat */
    pclient->stat.nb_call_total += 1;
    inc_func_call(pclient, CACHE_INODE_LOCK_CREATE);

    /* pentry should be there */
    if(pentry == NULL)
        {
            /* stat */
            inc_func_err_unrecover(pclient, CACHE_INODE_LOCK_CREATE);

            *pstatus = CACHE_INODE_INVALID_ARGUMENT;
            return *pstatus;
        }

    /* pentry should be a file */
    if(pentry->internal_md.type != REGULAR_FILE)
        {
            /* stat */
            inc_func_err_unrecover(pclient, CACHE_INODE_LOCK_CREATE);

            *pstatus = CACHE_INODE_BAD_TYPE;
            return *pstatus;
        }

    /* Use absolute offset, manage CACHE_INODE_LOCK_OFFSET_EOF here */
    if(length == CACHE_INODE_LOCK_OFFSET_EOF)
        {
            if(offset > pentry->object.file.attributes.filesize)
                {
                    /* stat */
                    inc_func_err_unrecover(pclient, CACHE_INODE_LOCK_CREATE);
 
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
    cache_inode_lock_check_conflicting_range(pentry,
                                             offset,
                                             abslength,
                                             lock_type,
                                             ppfilelock, pstatus);
    if(*pstatus != CACHE_INODE_SUCCESS)
        {
            /* stat */
            inc_func_err_unrecover(pclient, CACHE_INODE_LOCK_CREATE);
            V_w(&pentry->lock);

            return *pstatus;
        }

    /* Get a new lock */
    GetFromPool(pfilelock, &pclient->pool_state_v4, cache_inode_state_t);

    if(pfilelock == NULL)
        {
            LogDebug(COMPONENT_CACHE_INODE,
                     "Can't allocate a new file lock from cache pool");
            *pstatus = CACHE_INODE_MALLOC_ERROR;

            /* stat */
            inc_func_err_unrecover(pclient, CACHE_INODE_LOCK_CREATE);
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
    inc_func_success(pclient, CACHE_INODE_LOCK_CREATE);

    V_w(&pentry->lock);
    cache_inode_lock_print(pentry);

    *ppnewlock = pfilelock;
    *pstatus = CACHE_INODE_SUCCESS;
    return *pstatus;
#endif
}                               /* cache_inode_lock_create */
