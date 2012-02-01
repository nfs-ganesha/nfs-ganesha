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

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include "LRU_List.h"
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "cache_inode.h"
#include "stuff_alloc.h"

/**
 *
 * cache_inode_access: checks for an entry accessibility.
 *
 * Checks for an entry accessibility.
 *
 * @param pentry      [IN]    entry pointer for the fs object to be checked.
 * @param access_type [IN]    flags used to describe the kind of access to be checked.
 * @param pclient     [INOUT] ressource allocated by the client for the nfs management.
 * @param creds       [IN]    client credentials
 * @param pstatus     [OUT]   returned status.
 * @param use_mutex   [IN]    a flag to tell if mutex are to be used or not.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry \n
 * @return any other values show an unauthorized access.
 *
 */
cache_inode_status_t
cache_inode_access_sw(cache_entry_t *pentry,
                      fsal_accessflags_t access_type,
                      cache_inode_client_t *pclient,
                      struct user_cred *creds,
                      cache_inode_status_t *pstatus,
                      int use_mutex)
{
     fsal_attrib_list_t attr;
     fsal_status_t fsal_status;
     fsal_accessflags_t used_access_type;
     struct fsal_obj_handle *pfsal_handle = pentry->obj_handle;

     LogFullDebug(COMPONENT_CACHE_INODE,
                  "cache_inode_access_sw: access_type=0X%x",
                  access_type);

     /* Set the return default to CACHE_INODE_SUCCESS */
     *pstatus = CACHE_INODE_SUCCESS;

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

	  /* We get the attributes */
	  attr = pentry->obj_handle->attributes;


/** @TODO There is something way too clever with this use_test_access
 * flag.  If the flag is set, the FSAL_IS_ERROR is testing an uninitialized
 * fsal_status.  Also, the 'then' part is a NOP given the line above.
 * we will get the deref right for now.  The issue in the comment is solved
 * at the fsal level anyway because we have the access method but the fsal
 * writer makes the decision on how it is to be handled (locally in the fsal
 * or using the supplied common. This is also another struct copy...
 */
          if(pclient->use_test_access == 1) {
               /* We actually need the lock here since we're using
                  the attribute cache, so get it if the caller didn't
                  acquire it.  */
               if(use_mutex) {
                    if ((*pstatus
                         = cache_inode_lock_trust_attrs(pentry,
                                                        pcontext,
                                                        pclient))
                        != CACHE_INODE_SUCCESS) {
                         goto out;
                    }
               }
	       fsal_status = pfsal_handle->ops->getattrs(pfsal_handle, &attr);
               if (use_mutex) {
                    pthread_rwlock_unlock(&pentry->attr_lock);
               }
          } else {
               /* There is no reason to hold the mutex here, since we
                  aren't doing anything with cached attributes. */
	       fsal_status = pfsal_handle->ops->test_access(pfsal_handle,
							    creds,
							    used_access_type);
          }

          if(FSAL_IS_ERROR(fsal_status)) {
               *pstatus = cache_inode_error_convert(fsal_status);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(pentry, pclient);
               }
          } else {
               *pstatus = CACHE_INODE_SUCCESS;
          }
     }

out:
     return *pstatus;
}

/**
 *
 * cache_inode_access_no_mutex: checks for an entry accessibility. No mutex management
 *
 * Checks for an entry accessibility.
 *
 * @param pentry      [IN]    entry pointer for the fs object to be checked.
 * @param access_type [IN]    flags used to describe the kind of access to be checked.
 * @param pclient     [INOUT] ressource allocated by the client for the nfs management.
 * @param creds       [IN]    client credentials
 * @param pstatus     [OUT]   returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t
cache_inode_access_no_mutex(cache_entry_t * pentry,
                            fsal_accessflags_t access_type,
                            cache_inode_client_t * pclient,
			    struct user_cred *creds,
                            cache_inode_status_t * pstatus)
{
    return cache_inode_access_sw(pentry, access_type,
                                 pclient, pcontext, pstatus, FALSE);
}

/**
 *
 * cache_inode_access: checks for an entry accessibility.
 *
 * Checks for an entry accessibility.
 *
 * @param pentry      [IN]    entry pointer for the fs object to be checked.
 * @param access_type [IN]    flags used to describe the kind of access to be checked.
 * @param pclient     [INOUT] ressource allocated by the client for the nfs management.
 * @param creds       [IN]    client credentials
 * @param pstatus     [OUT]   returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t
cache_inode_access(cache_entry_t * pentry,
                   fsal_accessflags_t access_type,
                   cache_inode_client_t * pclient,
		   struct user_cred *creds,
                   cache_inode_status_t * pstatus)
{
    return cache_inode_access_sw(pentry, access_type,
                                 pclient, pcontext, pstatus, TRUE);
}
