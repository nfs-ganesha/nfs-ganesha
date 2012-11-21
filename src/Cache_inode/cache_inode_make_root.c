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
 * @defgroup Cache_inode Cache Inode
 * @{
 */

/**
 * @file cache_inode_make_root.c
 * @brief Insert the root of an export
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
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
 * @param[in]  root_hdl   Handle for the root
 * @param[out] root_entry The newly-created root
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */

cache_inode_status_t cache_inode_make_root(struct fsal_obj_handle *root_hdl,
					   cache_entry_t **root_entry)
{
  cache_inode_status_t status = CACHE_INODE_SUCCESS;

/** this used to be get but we get passed a handle so now new_entry */
  status = cache_inode_new_entry(root_hdl,
				 CACHE_INODE_FLAG_NONE,
				 root_entry);
  if(*root_entry != NULL)
    {
      /* The root directory is its own parent.  (Even though this is a
         weakref, it shouldn't be broken in practice.) */
      PTHREAD_RWLOCK_wrlock(&(*root_entry)->content_lock);
      (*root_entry)->object.dir.parent = (*root_entry)->weakref;
      (*root_entry)->object.dir.root = true;
      PTHREAD_RWLOCK_unlock(&(*root_entry)->content_lock);
    } else {
      LogCrit(COMPONENT_CACHE_INODE,
              "Unable to add root entry to cache, status = %s",
              cache_inode_err_str(status));
    }

  return status;
}                               /* cache_inode_make_root */
/** @} */
