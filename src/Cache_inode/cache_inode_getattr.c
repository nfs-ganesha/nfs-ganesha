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
#include "log.h"
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
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials
 * @param pstatus [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t
cache_inode_getattr(cache_entry_t *pentry,
                    fsal_attrib_list_t *pattr, /* XXX Change this so
                                                * we don't just copy
                                                * stuff on the stack. */
                    cache_inode_client_t *pclient,
                    cache_inode_status_t *pstatus)
{

     /* sanity check */
     if(pentry == NULL || pattr == NULL || pclient == NULL) {
          *pstatus = CACHE_INODE_INVALID_ARGUMENT;
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_getattr: returning "
                   "CACHE_INODE_INVALID_ARGUMENT because of bad arg");
          goto out;
     }

     /* Set the return default to CACHE_INODE_SUCCESS */
     *pstatus = CACHE_INODE_SUCCESS;

/* Lock (and refresh if necessary) the attributes, copy them out, and
   unlock. */

     if ((*pstatus
          = cache_inode_lock_trust_attrs(pentry,
                                         pclient))
         != CACHE_INODE_SUCCESS) {
          goto out;
     }
     *pattr = pentry->obj_handle->attributes;

     pthread_rwlock_unlock(&pentry->attr_lock);

out:

    return *pstatus;
}
