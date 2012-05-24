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
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 * cache_inode_make_root: Inserts the root of a FS in the cache.
 *
 * Inserts the root of a FS in the cache. This function will be called at junction traversal.
 *
 * @param pfsdata [IN] FSAL data for the root.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pstatus [OUT] returned status.
 */

cache_entry_t *cache_inode_make_root(struct fsal_obj_handle *root_hdl,
                                     cache_inode_client_t * pclient,
                                     cache_inode_status_t * pstatus)
{
  cache_entry_t *pentry = NULL;
  /* sanity check */
  if(pstatus == NULL)
    return NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

/** this used to be get but we get passed a handle so now new_entry */
  if((pentry = cache_inode_new_entry(root_hdl,
				     pclient,
				     CACHE_INODE_FLAG_NONE,
				     pstatus)) != NULL)
    {
      /* The root directory is its own parent.  (Even though this is a
         weakref, it shouldn't be broken in practice.) */
      pthread_rwlock_wrlock(&pentry->content_lock);
      pentry->object.dir.parent = pentry->weakref;
      pentry->object.dir.root = TRUE;
      pthread_rwlock_unlock(&pentry->content_lock);
    } else {
      LogCrit(COMPONENT_CACHE_INODE,
	      "Unable to add root entry to cache, status = %s",
	      cache_inode_err_str(*pstatus));
    }

  return pentry;
}                               /* cache_inode_make_root */
