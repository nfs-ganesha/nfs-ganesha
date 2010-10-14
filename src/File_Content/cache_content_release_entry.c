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
 * \file    cache_content_release_entry.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:33 $
 * \version $Revision: 1.7 $
 * \brief   Management of the file content cache: release an entry.
 *
 * cache_content_release_entry.c : Management of the file content cache: release an entry.
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
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

/**
 *
 * cache_content_release_entry: removes an entry from the cache and free the associated resources.
 *
 * Removes an entry from the cache and free the associated resources.
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is 
 * locked and will prevent from concurent accesses.
 *
 * @param pentry [IN] entry in file content layer for this file.
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful, other values show an error.
 *
 */
cache_content_status_t cache_content_release_entry(cache_content_entry_t * pentry,
                                                   cache_content_client_t * pclient,
                                                   cache_content_status_t * pstatus)
{
  /* By default, operation status is successful */
  *pstatus = CACHE_CONTENT_SUCCESS;

  /* stat */
  pclient->stat.func_stats.nb_call[CACHE_CONTENT_RELEASE_ENTRY] += 1;

  /* Remove the link between the Cache Inode entry and the File Content entry */
  pentry->pentry_inode->object.file.pentry_content = NULL;

  /* close the associated opened file */
  if(pentry->local_fs_entry.opened_file.local_fd > 0)
    {
      close(pentry->local_fs_entry.opened_file.local_fd);
      pentry->local_fs_entry.opened_file.last_op = 0;
    }

  /* Finally puts the entry back to entry pool for future use */
  ReleaseToPool(pentry, &pclient->content_pool);

  /* Remove the index file */
  if(unlink(pentry->local_fs_entry.cache_path_index) != 0)
    {
      if(errno != ENOENT)
        LogEvent(COMPONENT_CACHE_CONTENT,
                          "cache_content_release_entry: error when unlinking index file %s, errno = ( %d, '%s' )",
                          pentry->local_fs_entry.cache_path_index,
                          errno, strerror(errno));
    }

  /* Remove the data file */
  if(unlink(pentry->local_fs_entry.cache_path_data) != 0)
    {
      if(errno != ENOENT)
        LogEvent(COMPONENT_CACHE_CONTENT,
                          "cache_content_release_entry: error when unlinking index file %s, errno = ( %d, '%s' )",
                          pentry->local_fs_entry.cache_path_data, errno, strerror(errno));
    }

  return *pstatus;
}                               /* cache_content_release_entry */
