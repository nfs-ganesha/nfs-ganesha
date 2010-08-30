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
 * \file    cache_inode_getattr.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.17 $
 * \brief   Gets the attributes for an entry.
 *
 * cache_inode_getattr.c : Gets the attributes for an entry.
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

/**
 *
 * cache_inode_getattr: Gets the attributes for a cached entry.
 *
 * Gets the attributes for a cached entry. The FSAL attributes are kept in a structure when the entry
 * is added to the cache.
 *
 * @param pentry [IN] entry to be managed.
 * @param pattr [OUT] pointer to the results
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials
 * @param pstatus [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t
cache_inode_getattr(cache_entry_t * pentry,
                    fsal_attrib_list_t * pattr,
                    hash_table_t * ht, /* Unused, kept for protototype's homogeneity */
                    cache_inode_client_t * pclient,
                    fsal_op_context_t * pcontext,
                    cache_inode_status_t * pstatus)
{
    cache_inode_status_t status;
    fsal_handle_t *pfsal_handle;
    fsal_status_t fsal_status;

    /* sanity check */
    if(pentry == NULL || pattr == NULL ||
       ht == NULL || pclient == NULL || pcontext == NULL)
        {
            *pstatus = CACHE_INODE_INVALID_ARGUMENT;
            return *pstatus;
        }

    /* Set the return default to CACHE_INODE_SUCCESS */
    *pstatus = CACHE_INODE_SUCCESS;

    /* stats */
    pclient->stat.nb_call_total += 1;
    inc_func_call(pclient, CACHE_INODE_GETATTR);

    /* Lock the entry */
    P_w(&pentry->lock);
    status = cache_inode_renew_entry(pentry, pattr, ht,
                                     pclient, pcontext, pstatus);
    if(status != CACHE_INODE_SUCCESS)
        {
            V_w(&pentry->lock);
            inc_func_err_retryable(pclient, CACHE_INODE_GETATTR);
            return *pstatus;
        }

    /* RW Lock goes for writer to reader */
    rw_lock_downgrade(&pentry->lock);

    cache_inode_get_attributes(pentry, pattr);

    if(FSAL_TEST_MASK(pattr->asked_attributes,
                      FSAL_ATTR_RDATTR_ERR))
        {
            switch (pentry->internal_md.type)
                {
                case REGULAR_FILE:
                    pfsal_handle = &pentry->object.file.handle;
                    break;

                case SYMBOLIC_LINK:
                    pfsal_handle = &pentry->object.symlink.handle;
                    break;

                case DIR_BEGINNING:
                    pfsal_handle = &pentry->object.dir_begin.handle;
                    break;

                case DIR_CONTINUE:
                    /*
                     * lock the related dir_begin (dir begin are garbagge
                     * collected AFTER their related dir_cont)
                     * this means that if a DIR_CONTINUE exists,
                     * its pdir pointer is not endless
                     */
                    P_r(&pentry->object.dir_cont.pdir_begin->lock);
                    pfsal_handle = &pentry->object.dir_cont.pdir_begin->object.dir_begin.handle;
                    V_r(&pentry->object.dir_cont.pdir_begin->lock);
                    break;

                case SOCKET_FILE:
                case FIFO_FILE:
                case BLOCK_FILE:
                case CHARACTER_FILE:
                    pfsal_handle = &pentry->object.special_obj.handle;
                    break;
                }

            /*
             * An error occured when trying to get
             * the attributes, they have to be renewed
             */
            fsal_status = FSAL_getattrs(pfsal_handle, pcontext, pattr);
            if(FSAL_IS_ERROR(fsal_status))
                {
                    *pstatus = cache_inode_error_convert(fsal_status);
                    V_r(&pentry->lock);

                    if(fsal_status.major == ERR_FSAL_STALE)
                        {
                            cache_inode_status_t kill_status;

                            LogDebug(COMPONENT_CACHE_INODE_GC,
                                     "cache_inode_getattr: Stale FSAL File "
                                     "Handle detected for pentry = %p",
                                     pentry);

                            cache_inode_kill_entry(pentry, ht,
                                                   pclient, &kill_status);
                            if(kill_status != CACHE_INODE_SUCCESS)
                                LogCrit(COMPONENT_CACHE_INODE_GC,
                                        "cache_inode_getattr: Could not kill "
                                        "entry %p, status = %u",
                                        pentry, kill_status);

                            *pstatus = CACHE_INODE_FSAL_ESTALE;
                        }

                    /* stat */
                    inc_func_err_unrecover(pclient, CACHE_INODE_GETATTR);
                    return *pstatus;
                }

            /* Set the new attributes */
            cache_inode_set_attributes(pentry, pattr);
        }
    *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_GET, pclient);

    V_r(&pentry->lock);

    /* stat */
    if(*pstatus != CACHE_INODE_SUCCESS)
        inc_func_err_retryable(pclient, CACHE_INODE_GETATTR);
    else
        inc_func_success(pclient, CACHE_INODE_GETATTR);

    return *pstatus;
}
