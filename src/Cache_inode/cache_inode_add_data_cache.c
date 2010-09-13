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
 * \file    cache_inode_add_data_cache.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.4 $
 * \brief   Associates a File Content cache entry
 * to a pentry of type REGULAR FILE.
 *
 * cache_inode_add_data_cache.c : Associates a File Content
 * cache entry to a pentry of type REGULAR FILE.
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
#include "cache_content.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

cache_inode_status_t
cache_inode_add_data_cache(cache_entry_t * pentry,
                           hash_table_t * ht,
                           cache_inode_client_t * pclient,
                           fsal_op_context_t * pcontext,
                           cache_inode_status_t * pstatus)
{
    cache_content_status_t cache_content_status;
    cache_content_entry_t *pentry_content = NULL;
    cache_content_client_t *pcontent_client;

    /* Set the return default to CACHE_INODE_SUCCESS */
    *pstatus = CACHE_INODE_SUCCESS;

    /* stats */
    pclient->stat.nb_call_total += 1;
    inc_func_call(pclient, CACHE_INODE_ADD_DATA_CACHE);

    P_w(&pentry->lock);
    /* Operate only on a regular file */
    if(pentry->internal_md.type != REGULAR_FILE)
        {
            *pstatus = CACHE_INODE_BAD_TYPE;
            V_w(&pentry->lock);

            /* stats */
            inc_func_err_unrecover(pclient,
                                   CACHE_INODE_ADD_DATA_CACHE);
            return *pstatus;
        }
    if(pentry->object.file.pentry_content != NULL)
        {
            /* The object is already cached */
            *pstatus = CACHE_INODE_CACHE_CONTENT_EXISTS;
            V_w(&pentry->lock);

            /* stats */
            inc_func_err_retryable(pclient,
                                   CACHE_INODE_ADD_DATA_CACHE);
            return *pstatus;
        }
    pcontent_client = (cache_content_client_t *) pclient->pcontent_client;
    pentry_content = cache_content_new_entry(pentry, NULL, pcontent_client,
                                             ADD_ENTRY, pcontext,
                                             &cache_content_status);
    if(pentry_content == NULL)
        {
            *pstatus = cache_content_error_convert(cache_content_status);
            V_w(&pentry->lock);

            /* stats */
            inc_func_err_unrecover(pclient,
                                   CACHE_INODE_ADD_DATA_CACHE);
            return *pstatus;
        }

    /* Attached the entry to the cache inode */
    pentry->object.file.pentry_content = pentry_content;

    V_w(&pentry->lock);
    *pstatus = CACHE_INODE_SUCCESS;

    /* stats */
    inc_func_err_unrecover(pclient,
                           CACHE_INODE_ADD_DATA_CACHE);
    return *pstatus;
}
