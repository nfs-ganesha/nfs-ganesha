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
 * \file    cache_inode_make_root.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.12 $
 * \brief   Insert in the cache an entry that is the root of the FS cached.
 *
 * cache_inode_make_root.c : Inserts in the cache an entry that is the root of the FS cached.
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 * @brief Inserts the root of a FS in the cache.
 *
 * This ensures that the directory specified by fsdata is in the cache
 * and marks it as an export root.
 *
 * @param[in]  fsdata  Handle for the root
 * @param[in]  context FSAL credentials. Unused here
 * @param[out] status  Returned status
 *
 * @return the newly created cache entry.
 */

cache_entry_t *cache_inode_make_root(cache_inode_fsal_data_t *fsdata,
                                     fsal_op_context_t *context,
                                     cache_inode_status_t *status)
{
  cache_entry_t *entry = NULL;
  fsal_attrib_list_t attr;
  /* sanity check */
  if(status == NULL)
    return NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *status = CACHE_INODE_SUCCESS;

  if((entry = cache_inode_get(fsdata,
                              &attr,
                              context,
                              NULL,
                              status)) != NULL)
    {
      /* The root directory is its own parent.  (Even though this is a
         weakref, it shouldn't be broken in practice.) */
      PTHREAD_RWLOCK_WRLOCK(&entry->content_lock);
      entry->object.dir.parent = entry->weakref;
      entry->object.dir.root = TRUE;
      PTHREAD_RWLOCK_UNLOCK(&entry->content_lock);
    }

  return entry;
}                               /* cache_inode_make_root */
