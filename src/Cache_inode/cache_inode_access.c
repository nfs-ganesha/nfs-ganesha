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
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "abstract_mem.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * @brief Checks the permissions on an object
 *
 * This function returns success if the supplied credentials possess
 * permission required to meet the specified access.
 *
 * @param[in]  entry       The object to be checked
 * @param[in]  access_type The kind of access to be checked
 * @param[in]  context     FSAL context
 * @param[out] status      Returned status
 * @param[in]  use_mutex   Whether to acquire a read lock
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 *
 */
cache_inode_status_t
cache_inode_access_sw(cache_entry_t *entry,
                      fsal_accessflags_t access_type,
                      fsal_op_context_t *context,
                      cache_inode_status_t *status,
                      int use_mutex)
{
     fsal_status_t fsal_status;
     fsal_accessflags_t used_access_type;

     LogFullDebug(COMPONENT_CACHE_INODE,
                  "cache_inode_access_sw: access_type=0X%x",
                  access_type);

     /* Set the return default to CACHE_INODE_SUCCESS */
     *status = CACHE_INODE_SUCCESS;

     /*
      * We do no explicit access test in FSAL for FSAL_F_OK: it is
      * considered that if an entry resides in the cache_inode, then a
      * FSAL_getattrs was successfully made to populate the cache entry,
      * this means that the entry exists. For this reason, F_OK is
      * managed internally
      */
     if(access_type != FSAL_F_OK) {
          /* We get ride of F_OK */
          used_access_type = access_type & ~FSAL_F_OK;

          /*
           * Function FSAL_test_access is used instead of FSAL_access.
           * This allow to take benefit of the previously cached
           * attributes. This behavior is configurable via the
           * configuration file.
           */

          if(cache_inode_params.use_test_access == 1) {
               /* We actually need the lock here since we're using
                  the attribute cache, so get it if the caller didn't
                  acquire it.  */
               if(use_mutex) {
                    if ((*status
                         = cache_inode_lock_trust_attrs(entry,
                                                        context))
                        != CACHE_INODE_SUCCESS) {
                         goto out;
                    }
               }
               fsal_status
                    = FSAL_test_access(context,
                                       used_access_type,
                                       &entry->attributes);
               if (use_mutex) {
                    pthread_rwlock_unlock(&entry->attr_lock);
               }
          } else {
               /* There is no reason to hold the mutex here, since we
                  aren't doing anything with cached attributes. */
                    fsal_status = FSAL_access(&entry->handle, context,
                                              used_access_type, NULL);
          }

          if(FSAL_IS_ERROR(fsal_status)) {
               *status = cache_inode_error_convert(fsal_status);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    LogEvent(COMPONENT_CACHE_INODE,
                       "STALE returned by FSAL, calling kill_entry");
                    cache_inode_kill_entry(entry);
               }
          } else {
               *status = CACHE_INODE_SUCCESS;
          }
     }

out:
     return *status;
}

/**
 *
 * @brief Checks entry permissions without taking a lock
 *
 * This function checks whether the specified permissions are
 * available on the object.  This function may only be called if an
 * attribute lock is held.
 *
 * @param[in]  entry       entry pointer for the fs object to be checked.
 * @param[in]  access_type The kind of access to be checked
 * @param[in]  context     FSAL credentials
 * @param[out] status      Returned status
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 *
 */
cache_inode_status_t
cache_inode_access_no_mutex(cache_entry_t *entry,
                            fsal_accessflags_t access_type,
                            fsal_op_context_t *context,
                            cache_inode_status_t *status)
{
    return cache_inode_access_sw(entry, access_type,
                                 context, status, FALSE);
}

/**
 *
 * @brief Checks permissions on an entry
 *
 * This function acquires the attribute lock on the supplied cach
 * entry then checks if the supplied credentials are sufficient to
 * gain the supplied access.
 *
 * @param[in] entry       The object to be checked
 * @param[in] access_type The kind of access to be checked
 * @param[in] context     FSAL credentials
 * @param[in] status      Returned status
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 */
cache_inode_status_t
cache_inode_access(cache_entry_t *entry,
                   fsal_accessflags_t access_type,
                   fsal_op_context_t *context,
                   cache_inode_status_t *status)
{
    return cache_inode_access_sw(entry, access_type,
                                 context, status, TRUE);
}
