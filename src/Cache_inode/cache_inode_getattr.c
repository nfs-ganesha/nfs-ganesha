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

#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "nfs_exports.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * @brief Gets the attributes for a cached entry
 *
 * Gets the attributes for a cached entry. The FSAL attributes are
 * kept in a structure when the entry is added to the cache.
 * Currently this structure is copied out to the caller after possibly
 * being reloaded from the FSAL.
 *
 * @param[in]  entry   Entry to be managed.
 * @param[out] attr    Pointer to the results
 * @param[in]  context FSAL credentials
 * @param[out] status  Returned status
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 *
 */
cache_inode_status_t
cache_inode_getattr(cache_entry_t *entry,
                    fsal_attrib_list_t *attr, /* XXX Change this so
                                                * we don't just copy
                                                * stuff on the stack. */
                    fsal_op_context_t *context,
                    cache_inode_status_t *status)
{
     /* sanity check */
     if(entry == NULL || attr == NULL || context == NULL) {
          *status = CACHE_INODE_INVALID_ARGUMENT;
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_getattr: returning "
                   "CACHE_INODE_INVALID_ARGUMENT because of bad arg");
          return *status;
     }

     /* Set the return default to CACHE_INODE_SUCCESS */
     *status = CACHE_INODE_SUCCESS;

/* Lock (and refresh if necessary) the attributes, copy them out, and
   unlock. */

     if ((*status
          = cache_inode_lock_trust_attrs(entry,
                                         context))
         != CACHE_INODE_SUCCESS) {
          goto out;
     }

     *attr = entry->attributes;
     set_mounted_on_fileid(entry, attr, context->export_context->fe_export);

     PTHREAD_RWLOCK_UNLOCK(&entry->attr_lock);

out:

    return *status;
}
