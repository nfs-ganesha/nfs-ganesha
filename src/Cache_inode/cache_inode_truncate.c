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
 * \file    cache_inode_truncate.c
 * \date    $Date: 2005/11/28 17:02:28 $
 * \version $Revision: 1.19 $
 * \brief   Truncates a regular file.
 *
 * cache_inode_truncate.c : Truncates a regular file.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "fsal.h"
#include "log.h"
#include "HashData.h"
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
 * @param[in]  context FSAL credentials
 * @param[out] status  Returned status
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 */

cache_inode_status_t
cache_inode_truncate_impl(cache_entry_t *entry,
                          uint64_t length,
                          struct req_op_context *req_ctx,
                          cache_inode_status_t *status)
{
  fsal_status_t fsal_status;
  struct fsal_obj_handle *obj_hdl = entry->obj_handle;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *status = CACHE_INODE_SUCCESS;

  /* Only regular files can be truncated */
  if(entry->type != REGULAR_FILE)
    {
      *status = CACHE_INODE_BAD_TYPE;
      return *status;
    }

  /* You have to be able to write the file to truncate it
   */
  fsal_status = obj_hdl->ops->test_access(obj_hdl, req_ctx, FSAL_W_OK);
  if(FSAL_IS_ERROR(fsal_status)) {
    *status = cache_inode_error_convert(fsal_status);
    return *status;
  }

  /* Call FSAL to actually truncate */
  fsal_status = obj_hdl->ops->truncate(obj_hdl, length);
  if( !FSAL_IS_ERROR(fsal_status)) {
    fsal_status = obj_hdl->ops->getattrs(obj_hdl);
  }


  if(FSAL_IS_ERROR(fsal_status))
    {
      *status = cache_inode_error_convert(fsal_status);
      if (fsal_status.major == ERR_FSAL_STALE) {
        cache_inode_kill_entry(entry);
      }
      return *status;
    }

  return *status;
}                               /* cache_inode_truncate_sw */

/**
 *
 * @brief Truncates a regular file specified by its cache entry.
 *
 * Truncates a regular file specified by its cache entry.
 *
 * @param[in]  entry   The file to be truncated
 * @param[in]  length  New length for the file
 * @param[out] attr    Attrtibutes for the file after the operation
 * @param[in]  context FSAL credentials
 * @param[out] status  Returned status
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 *
 */
cache_inode_status_t
cache_inode_truncate(cache_entry_t *entry,
                     uint64_t length,
                     struct attrlist *attr,
                     struct req_op_context *req_ctx,
                     cache_inode_status_t *status)
{
  cache_inode_status_t ret;

  pthread_rwlock_wrlock(&entry->attr_lock);
  pthread_rwlock_wrlock(&entry->content_lock);
  ret = cache_inode_truncate_impl(entry, length, req_ctx, status);
  *attr = entry->obj_handle->attributes;
  pthread_rwlock_unlock(&entry->attr_lock);
  pthread_rwlock_unlock(&entry->content_lock);
  return ret;
} /* cache_inode_truncate */
