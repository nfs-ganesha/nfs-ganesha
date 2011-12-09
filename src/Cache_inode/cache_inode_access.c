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
 * \file    cache_inode_access.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.19 $
 * \brief   Check for object accessibility.
 *
 * cache_inode_access.c : Check for object accessibility.
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

/**
 *
 * cache_inode_access: checks for an entry accessibility.
 *
 * Checks for an entry accessibility.
 *
 * @param pentry      [IN]    entry pointer for the fs object to be checked.
 * @param access_type [IN]    flags used to describe the kind of access to be checked.
 * @param ht          [INOUT] hash table used for the cache.
 * @param pclient     [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext    [IN]    FSAL context
 * @param pstatus     [OUT]   returned status.
 * @param use_mutex   [IN]    a flag to tell if mutex are to be used or not.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry \n
 * @return any other values show an unauthorized access.
 *
 */
cache_inode_status_t
cache_inode_access_sw(cache_entry_t * pentry,
                      fsal_accessflags_t access_type,
                      hash_table_t * ht,
                      cache_inode_client_t * pclient,
                      fsal_op_context_t * pcontext,
                      cache_inode_status_t * pstatus, int use_mutex)
{
    fsal_attrib_list_t attr;
    fsal_status_t fsal_status;
    cache_inode_status_t cache_status;
    fsal_accessflags_t used_access_type;
    fsal_handle_t *pfsal_handle = NULL;

    LogFullDebug(COMPONENT_CACHE_INODE, "cache_inode_access_sw: access_type=0X%x",
                 access_type);

    /* Set the return default to CACHE_INODE_SUCCESS */
    *pstatus = CACHE_INODE_SUCCESS;

    /* stats */
    pclient->stat.nb_call_total += 1;
    inc_func_call(pclient, CACHE_INODE_ACCESS);

    if(use_mutex)
        P_r(&pentry->lock);
    /*
     * We do no explicit access test in FSAL for FSAL_F_OK:
     * it is considered that if an entry resides in the cache_inode,
     * then a FSAL_getattrs was successfully made to populate the
     * cache entry, this means that the entry exists. For this reason,
     * F_OK is managed internally
     */
    if(access_type != FSAL_F_OK)
        {
            /* We get ride of F_OK */
            used_access_type = access_type & ~FSAL_F_OK;

            /* We get the attributes */
            cache_inode_get_attributes(pentry, &attr);
            /*
             * Function FSAL_test_access is used instead of FSAL_access.
             * This allow to take benefit of the previously cached
             * attributes. This behavior is configurable via the
             * configuration file.
             */

            if(pclient->use_test_access == 1)
                {
                    /* We get the attributes */
                    cache_inode_get_attributes(pentry, &attr);

                    fsal_status = FSAL_test_access(pcontext, used_access_type, &attr);
                }
            else
                {
                    pfsal_handle = cache_inode_get_fsal_handle(pentry, pstatus);
                    if(pfsal_handle == NULL)
                        {
                            if(use_mutex)
                                V_r(&pentry->lock);
                            return *pstatus;
                        }
#ifdef _USE_MFSL
                    fsal_status = MFSL_access(&pentry->mobject, pcontext,
                                              &pclient->mfsl_context,
                                              used_access_type, &attr, NULL);
#else
                    fsal_status = FSAL_access(pfsal_handle, pcontext,
                                              used_access_type, &attr);
#endif
                }

            if(FSAL_IS_ERROR(fsal_status))
                {
                    *pstatus = cache_inode_error_convert(fsal_status);
                    inc_func_err_retryable(pclient, CACHE_INODE_ACCESS);

                    if(fsal_status.major == ERR_FSAL_STALE)
                        {
                            cache_inode_status_t kill_status;

                            LogEvent(COMPONENT_CACHE_INODE,
                                     "cache_inode_access: Stale FSAL File Handle detected for pentry = %p",
                                     pentry);

                            if( use_mutex )
                                 cache_inode_kill_entry( pentry, RD_LOCK, ht,
                                                         pclient, &kill_status);
                            else
                                 cache_inode_kill_entry( pentry, NO_LOCK, ht,
                                                         pclient, &kill_status);

                            if(kill_status != CACHE_INODE_SUCCESS)
                                LogCrit(COMPONENT_CACHE_INODE,
                                        "cache_inode_access: Could not kill entry %p, status = %u",
                                        pentry, kill_status);

                            *pstatus = CACHE_INODE_FSAL_ESTALE;
                            return *pstatus;
                        }
                }
            else
                *pstatus = CACHE_INODE_SUCCESS;

        }

    if(*pstatus != CACHE_INODE_SUCCESS)
        {
            if(use_mutex)
                V_r(&pentry->lock);

            return *pstatus;
        }
    /* stats and validation */
    cache_status = cache_inode_valid(pentry,
                                     CACHE_INODE_OP_GET,
                                     pclient);
    if(cache_status != CACHE_INODE_SUCCESS)
        inc_func_err_retryable(pclient, CACHE_INODE_ACCESS);
    else
        inc_func_success(pclient, CACHE_INODE_ACCESS);

    if(use_mutex)
        V_r(&pentry->lock);

    return *pstatus;
}

/**
 *
 * cache_inode_access_no_mutex: checks for an entry accessibility. No mutex management
 *
 * Checks for an entry accessibility.
 *
 * @param pentry      [IN]    entry pointer for the fs object to be checked.
 * @param access_type [IN]    flags used to describe the kind of access to be checked.
 * @param ht          [INOUT] hash table used for the cache.
 * @param pclient     [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext       [IN]    FSAL credentials
 * @param pstatus     [OUT]   returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t
cache_inode_access_no_mutex(cache_entry_t * pentry,
                            fsal_accessflags_t access_type,
                            hash_table_t * ht,
                            cache_inode_client_t * pclient,
                            fsal_op_context_t * pcontext,
                            cache_inode_status_t * pstatus)
{
    return cache_inode_access_sw(pentry, access_type, ht,
                                 pclient, pcontext, pstatus, FALSE);
}

/**
 *
 * cache_inode_access: checks for an entry accessibility.
 *
 * Checks for an entry accessibility.
 *
 * @param pentry      [IN]    entry pointer for the fs object to be checked.
 * @param access_type [IN]    flags used to describe the kind of access to be checked.
 * @param ht          [INOUT] hash table used for the cache.
 * @param pclient     [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext       [IN]    FSAL credentials
 * @param pstatus     [OUT]   returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t
cache_inode_access(cache_entry_t * pentry,
                   fsal_accessflags_t access_type,
                   hash_table_t * ht,
                   cache_inode_client_t * pclient,
                   fsal_op_context_t * pcontext,
                   cache_inode_status_t * pstatus)
{
    return cache_inode_access_sw(pentry, access_type, ht,
                                 pclient, pcontext, pstatus, TRUE);
}
