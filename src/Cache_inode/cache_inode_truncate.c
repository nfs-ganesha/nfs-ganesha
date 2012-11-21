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
 * @file    cache_inode_truncate.c
 * @brief   Truncates a regular file.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "fsal.h"
#include "log.h"
#include "HashTable.h"
#include "cache_inode.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 * @brief truncates a regular file
 *
 * This function truncates a regular file to the length specified.
 *
 * @param[in]  entry   The file to be truncated
 * @param[in]  length  New length for the file
 * @param[in]  req_ctx Request context
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 */

cache_inode_status_t
cache_inode_truncate_impl(cache_entry_t *entry,
                          uint64_t length,
                          struct req_op_context *req_ctx)
{
  fsal_status_t fsal_status;
  struct fsal_obj_handle *obj_hdl = entry->obj_handle;
  cache_inode_status_t status = CACHE_INODE_SUCCESS;

  /* Only regular files can be truncated */
  if(entry->type != REGULAR_FILE)
    {
      status = CACHE_INODE_BAD_TYPE;
      return status;
    }

  /* You have to be able to write the file to truncate it
   */
  fsal_status = obj_hdl->ops->test_access(obj_hdl, req_ctx, FSAL_W_OK);
  if(FSAL_IS_ERROR(fsal_status)) {
    status = cache_inode_error_convert(fsal_status);
    return status;
  }

  /* Call FSAL to actually truncate */
  fsal_status = obj_hdl->ops->truncate(obj_hdl, req_ctx, length);
  if (!FSAL_IS_ERROR(fsal_status)) {
    fsal_status = obj_hdl->ops->getattrs(obj_hdl, req_ctx);
  }

  if(FSAL_IS_ERROR(fsal_status))
    {
      status = cache_inode_error_convert(fsal_status);
      if (fsal_status.major == ERR_FSAL_STALE) {
        cache_inode_kill_entry(entry);
      }
      return status;
    }

  return status;
}

/**
 * @brief Truncates a regular file specified by its cache entry.
 *
 * Truncates a regular file specified by its cache entry.
 *
 * @param[in]  entry   The file to be truncated
 * @param[in]  length  New length for the file
 * @param[in]  req_ctx Request context
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 */
cache_inode_status_t
cache_inode_truncate(cache_entry_t *entry,
                     uint64_t length,
                     struct req_op_context *req_ctx)
{
  cache_inode_status_t status;

  PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
  PTHREAD_RWLOCK_wrlock(&entry->content_lock);
  status = cache_inode_truncate_impl(entry, length, req_ctx);
  PTHREAD_RWLOCK_unlock(&entry->attr_lock);
  PTHREAD_RWLOCK_unlock(&entry->content_lock);
  return status;
}
/** @} */
