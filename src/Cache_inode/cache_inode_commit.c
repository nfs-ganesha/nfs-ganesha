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
 * \file    cache_inode_commit.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:27 $
 * \version $Revision: 1.20 $
 * \brief   Commits an IO on a REGULAR_FILE.
 *
 * cache_inode_commit.c : Commits an IO on a REGULAR_FILE.
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
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_commit: commits a write operation on unstable storage
 *
 * Reads/Writes through the cache layer.
 *
 * @param pentry [IN] entry in cache inode layer whose content is to be accessed.
 * @param read_or_write [IN] a flag of type cache_content_io_direction_t to tell if a read or write is to be done.
 * @param seek_descriptor [IN] absolute position (in the FSAL file) where the IO will be done.
 * @param buffer_size [IN] size of the buffer pointed by parameter 'buffer'.
 * @param pio_size [OUT] the size of the io that was successfully made.
 * @param pfsal_attr [OUT] the FSAL attributes after the operation.
 * @param buffer write:[IN] read:[OUT] the buffer for the data.
 * @param ht [INOUT] the hashtable used for managing the cache.
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @param pcontext [IN] fsal context for the operation.
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 * @todo: BUGAZEOMEU; gestion de la taille du fichier a prendre en compte.
 *
 */

cache_inode_status_t
cache_inode_commit(cache_entry_t * pentry,
                   uint64_t offset,
                   fsal_size_t count,
                   fsal_attrib_list_t * pfsal_attr,
                   hash_table_t * ht,
                   cache_inode_client_t * pclient,
                   fsal_op_context_t * pcontext,
                   cache_inode_status_t * pstatus)
{
    cache_inode_status_t status;
    fsal_seek_t seek_descriptor;
    fsal_size_t size_io_done;
    fsal_boolean_t eof;
    cache_inode_unstable_data_t *udata;

    udata = &pentry->object.file.unstable_data;
    if(udata->buffer == NULL)
        {
            *pstatus = CACHE_INODE_SUCCESS;
            return *pstatus;
        }
    if(count == 0 || count == 0xFFFFFFFFL)
        {
            /* Count = 0 means "flush all data to permanent storage */
            seek_descriptor.offset = udata->offset;
            seek_descriptor.whence = FSAL_SEEK_SET;

            status = cache_inode_rdwr(pentry,
                                      CACHE_INODE_WRITE,
                                      &seek_descriptor, udata->length,
                                      &size_io_done, pfsal_attr,
                                      udata->buffer, &eof, ht,
                                      pclient, pcontext, TRUE, pstatus);
            if (status != CACHE_INODE_SUCCESS)
                return *pstatus;

            P_w(&pentry->lock);

            Mem_Free(udata->buffer);
            udata->buffer = NULL;

            V_w(&pentry->lock);
        }
    else
        {
            if(offset < udata->offset)
                {
                    *pstatus = CACHE_INODE_INVALID_ARGUMENT;
                    return *pstatus;
                }

            seek_descriptor.offset = offset;
            seek_descriptor.whence = FSAL_SEEK_SET;

            return cache_inode_rdwr(pentry,
                                    CACHE_INODE_WRITE,
                                    &seek_descriptor,
                                    count,
                                    &size_io_done,
                                    pfsal_attr,
                                    (char *)(udata->buffer + offset - udata->offset),
                                    &eof, ht, pclient,
                                    pcontext, TRUE, pstatus);
    }
  /* Regulat exit */
  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}
