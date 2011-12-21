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
#include <assert.h>

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
    fsal_handle_t *pfsal_handle = NULL;
    fsal_status_t fsal_status;

    /* sanity check */
    if(pentry == NULL || pattr == NULL ||
       ht == NULL || pclient == NULL || pcontext == NULL)
        {
            *pstatus = CACHE_INODE_INVALID_ARGUMENT;
            LogDebug(COMPONENT_CACHE_INODE,
                     "cache_inode_getattr: returning CACHE_INODE_INVALID_ARGUMENT because of bad arg");
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
            LogFullDebug(COMPONENT_CACHE_INODE,
                         "cache_inode_getattr: returning %d(%s) from cache_inode_renew_entry",
                         *pstatus, cache_inode_err_str(*pstatus));
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
                    assert(pentry->object.symlink);
                    pfsal_handle = &pentry->object.symlink->handle;
                    break;

                case DIRECTORY:
                    pfsal_handle = &pentry->object.dir.handle;
                    break;
                case SOCKET_FILE:
                case FIFO_FILE:
                case BLOCK_FILE:
                case CHARACTER_FILE:
                    pfsal_handle = &pentry->object.special_obj.handle;
                    break;
                case FS_JUNCTION:
                case UNASSIGNED:
                case RECYCLED:
                    *pstatus = CACHE_INODE_INVALID_ARGUMENT;
                    LogFullDebug(COMPONENT_CACHE_INODE,
                                 "cache_inode_getattr: returning %d(%s) from cache_inode_renew_entry - unexpected md_type",
                                 *pstatus, cache_inode_err_str(*pstatus));
                    return *pstatus;
                }

            /*
             * An error occured when trying to get
             * the attributes, they have to be renewed
             */
#ifdef _USE_MFSL
            fsal_status = FSAL_getattrs_descriptor(&(cache_inode_fd(pentry)->fsal_file), pfsal_handle, pcontext, pattr);
#else
            fsal_status = FSAL_getattrs_descriptor(cache_inode_fd(pentry), pfsal_handle, pcontext, pattr);
#endif
            if(FSAL_IS_ERROR(fsal_status))
                {
                    *pstatus = cache_inode_error_convert(fsal_status);
                    
                    V_r(&pentry->lock);

                    if(fsal_status.major == ERR_FSAL_STALE)
                        {
                            cache_inode_status_t kill_status;

                            LogEvent(COMPONENT_CACHE_INODE,
                                     "cache_inode_getattr: Stale FSAL File Handle detected for pentry = %p",
                                     pentry);

                            /* Locked flag is set to true to show entry has a read lock */
                            cache_inode_kill_entry( pentry, WT_LOCK, ht,
                                                    pclient, &kill_status);
                            if(kill_status != CACHE_INODE_SUCCESS)
                                LogCrit(COMPONENT_CACHE_INODE,
                                        "cache_inode_getattr: Could not kill entry %p, status = %u",
                                        pentry, kill_status);

                            *pstatus = CACHE_INODE_FSAL_ESTALE;
                        }

                    /* stat */
                    inc_func_err_unrecover(pclient, CACHE_INODE_GETATTR);
                    LogDebug(COMPONENT_CACHE_INODE,
                             "cache_inode_getattr: returning %d(%s) from FSAL_getattrs_descriptor",
                             *pstatus, cache_inode_err_str(*pstatus));
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

#ifdef _USE_NFS4_ACL
    if(isDebug(COMPONENT_NFS_V4_ACL))
      {
        LogDebug(COMPONENT_CACHE_INODE,
                 "cache_inode_getattr: pentry = %p, acl = %p",
                 pentry, pattr->acl);

        if(pattr->acl)
          {
            fsal_ace_t *pace;
            for(pace = pattr->acl->aces; pace < pattr->acl->aces + pattr->acl->naces; pace++)
              {
                LogDebug(COMPONENT_CACHE_INODE,
                         "cache_inode_getattr: ace type = 0x%x, flag = 0x%x, perm = 0x%x, special = %d, %s = 0x%x",
                         pace->type, pace->flag, pace->perm, IS_FSAL_ACE_SPECIAL_ID(*pace),
                         GET_FSAL_ACE_WHO_TYPE(*pace), GET_FSAL_ACE_WHO(*pace));
              }
          }
      }
#endif                          /* _USE_NFS4_ACL */

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "cache_inode_getattr: returning %d(%s) from cache_inode_valid",
                 *pstatus, cache_inode_err_str(*pstatus));
    return *pstatus;
}
