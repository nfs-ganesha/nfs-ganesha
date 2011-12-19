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
 * \author  $Author: deniel $
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
#include "LRU_List.h"
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
 * @brief truncates a regular file specified by its cache entry.
 *
 * Truncates a regular file specified by its cache entry.
 *
 * @param pentry [INOUT] Entry pointer for the fs object to be truncated.
 * @param length [IN] Wanted length for the file.
 * @param pattr [OUT] Attrtibutes for the file after the operation.
 * @param pclient [INOUT] Structure for per-thread resource management
 * @param pcontext [IN] FSAL credentials
 * @param pstatus [OUT] Returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when
 *         validating the entry
 */

cache_inode_status_t
cache_inode_truncate_impl(cache_entry_t *pentry,
                          fsal_size_t length,
                          fsal_attrib_list_t *pattr,
                          cache_inode_client_t *pclient,
			  struct user_cred *creds,
                          cache_inode_status_t *pstatus)
{
  fsal_status_t fsal_status;
  struct fsal_obj_handle *obj_hdl = pentry->obj_handle;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Only regular files can be truncated */
  if(pentry->type != REGULAR_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* Call FSAL to actually truncate */
  obj_hdl->attributes.asked_attributes = pclient->attrmask;
  fsal_status = obj_hdl->ops->truncate(obj_hdl, length);
  if( !FSAL_IS_ERROR(fsal_status))
    fsal_status = obj_hdl->ops->getattrs(obj_hdl, pattr);
/*   if (pentry->object.file.open_fd.openflags == FSAL_O_CLOSED) */
/*     fd = NULL; */
/*   else */
/*     fd = &(pentry->object.file.open_fd.fd); */
/*   fsal_status = FSAL_truncate(&pentry->handle, pcontext, length, */
/*                               fd, /\* used by FSAL_GPFS *\/ */
/*                               &pentry->attributes); */

  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      if (fsal_status.major == ERR_FSAL_STALE) {
        cache_inode_kill_entry(pentry, pclient);
      }
      return *pstatus;
    }

/** @TODO  cleanup with purging of attribute copying 
 */
  /* Returns the attributes */
  *pattr = obj_hdl->attributes;

  return *pstatus;
}                               /* cache_inode_truncate_sw */

/**
 *
 * cache_inode_truncate: truncates a regular file specified by its cache entry.
 *
 * Truncates a regular file specified by its cache entry.
 *
 * @param pentry    [INOUT] entry pointer for the fs object to be truncated.
 * @param length    [IN]    wanted length for the file.
 * @param pattr     [OUT]   attrtibutes for the file after the operation.
 * @param pclient   [INOUT] ressource allocated by the client for the nfs management.
 * @param creds     [IN]    client user credentials 
 * @param pstatus   [OUT]   returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_truncate(cache_entry_t * pentry,
                                          fsal_size_t length,
                                          fsal_attrib_list_t * pattr,
                                          cache_inode_client_t * pclient,
                                          struct user_cred *creds,
                                          cache_inode_status_t * pstatus)
{
  cache_inode_status_t ret;

  pthread_rwlock_wrlock(&pentry->attr_lock);
  pthread_rwlock_wrlock(&pentry->content_lock);
  ret = cache_inode_truncate_impl(pentry,
                                   length, pattr, pclient, creds, pstatus);
  pthread_rwlock_unlock(&pentry->attr_lock);
  pthread_rwlock_unlock(&pentry->content_lock);
  return ret;
} /* cache_inode_truncate */
