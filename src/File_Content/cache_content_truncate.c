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
 * \file    cache_content_truncate.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:33 $
 * \version $Revision: 1.5 $
 * \brief   Management of the file content cache: truncate operation.
 *
 * cache_content_truncate.c : Management of the file content cache, truncate operation.
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

cache_content_status_t cache_content_truncate(cache_content_entry_t * pentry,
                                              fsal_size_t length,
                                              cache_content_client_t * pclient,
                                              cache_content_status_t * pstatus)
{
  *pstatus = CACHE_CONTENT_SUCCESS;

  /* Perform the truncate operation */
  if(truncate(pentry->local_fs_entry.cache_path_data, (off_t) length) != 0)
    {
      /* Operation failed */

      /* LOG */
      LogMajor(COMPONENT_CACHE_CONTENT,
                        "cache_content_truncate: impossible to truncate %s on local fs, error = ( %d, '%s' )",
                        pentry->local_fs_entry.cache_path_data, errno, strerror(errno));

      /* Sets the error */
      *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
    }

  return *pstatus;
}                               /* cache_content_truncate */
