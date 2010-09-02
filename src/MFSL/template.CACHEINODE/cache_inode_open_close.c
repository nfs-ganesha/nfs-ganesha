/**
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
 *
 * \file    cache_inode_open_close.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:27 $
 * \version $Revision: 1.20 $
 * \brief   Removes an entry of any type.
 *
 * cache_inode_rdwr.c : performs an IO on a REGULAR_FILE.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
#include <strings.h>

/**
 *
 * cache_content_open: opens the local fd on  the cache.
 *
 * Opens the fd on  the FSAL
 *
 * @param pentry    [IN]  entry in file content layer whose content is to be accessed.
 * @param pclient   [IN]  ressource allocated by the client for the nfs management.
 * @param openflags [IN]  flags to be used to open the file
 * @param pcontent  [IN]  FSAL operation context
 * @pstatus         [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 */

cache_inode_status_t cache_inode_open(cache_entry_t * pentry,
                                      cache_inode_client_t * pclient,
                                      fsal_openflags_t openflags,
                                      fsal_op_context_t * pcontext,
                                      cache_inode_status_t * pstatus)
{
  fsal_status_t fsal_status;

  if((pentry == NULL) || (pclient == NULL) || (pcontext == NULL) || (pstatus == NULL))
    return CACHE_INODE_INVALID_ARGUMENT;

  if(pentry->internal_md.type != REGULAR_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  if(pclient->use_cache == 0)
    pentry->object.file.open_fd.last_op = 0;    /* to force opening the file */

  if((pclient->use_cache == 1) &&
     (pentry->object.file.open_fd.fileno >= 0) &&
     (time(NULL) - pentry->object.file.open_fd.last_op > pclient->retention))
    {
      fsal_status = FSAL_close(&(pentry->object.file.open_fd.fd));
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          return *pstatus;
        }
      pentry->object.file.open_fd.last_op = 0;
    }

  /* Open file need to be close */
  if((pentry->object.file.open_fd.openflags != 0) &&
     (pentry->object.file.open_fd.fileno >= 0) &&
     (pentry->object.file.open_fd.openflags != openflags))
    {
      fsal_status = FSAL_close(&(pentry->object.file.open_fd.fd));
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          return *pstatus;
        }

    }

  if(pentry->object.file.open_fd.last_op == 0)
    {
      /* opened file is not preserved yet */
      fsal_status = FSAL_open(&(pentry->object.file.handle),
                              pcontext,
                              openflags,
                              &pentry->object.file.open_fd.fd,
                              &(pentry->object.file.attributes));

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          return *pstatus;
        }

      pentry->object.file.open_fd.fileno =
          (int)FSAL_FILENO(&(pentry->object.file.open_fd.fd));
      pentry->object.file.open_fd.openflags = openflags;
    }

  /* regular exit */

  pentry->object.file.open_fd.last_op = time(NULL);
  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;

}                               /* cache_content_open */

/**
 *
 * cache_content_open: opens the local fd on  the cache.
 *
 * Opens the fd on  the FSAL
 *
 * @param pentry_dir  [IN]  parent entry for the file
 * @param pname       [IN]  name of the file to be opened in the parent directory
 * @param pentry_file [IN]  file entry to be opened
 * @param pclient     [IN]  ressource allocated by the client for the nfs management.
 * @param openflags   [IN]  flags to be used to open the file 
 * @param pcontent    [IN]  FSAL operation context
 * @pstatus           [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 */

cache_inode_status_t cache_inode_open_by_name(cache_entry_t * pentry_dir,
                                              fsal_name_t * pname,
                                              cache_entry_t * pentry_file,
                                              cache_inode_client_t * pclient,
                                              fsal_openflags_t openflags,
                                              fsal_op_context_t * pcontext,
                                              cache_inode_status_t * pstatus)
{
  fsal_status_t fsal_status;
  fsal_size_t save_filesize;
  fsal_size_t save_spaceused;
  fsal_time_t save_mtime;

  if((pentry_dir == NULL) || (pname == NULL) || (pentry_file == NULL) ||
     (pclient == NULL) || (pcontext == NULL) || (pstatus == NULL))
    return CACHE_INODE_INVALID_ARGUMENT;

  if((pentry_dir->internal_md.type != DIR_BEGINNING)
     && (pentry_dir->internal_md.type != DIR_CONTINUE))
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  if(pentry_file->internal_md.type != REGULAR_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  if(pclient->use_cache == 0)
    pentry_file->object.file.open_fd.last_op = 0;       /* to force opening the file */

  if((pclient->use_cache == 1) &&
     (pentry_file->object.file.open_fd.fileno >= 0) &&
     (time(NULL) - pentry_file->object.file.open_fd.last_op > pclient->retention))
    {
      fsal_status = FSAL_close(&(pentry_file->object.file.open_fd.fd));
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          return *pstatus;
        }
      pentry_file->object.file.open_fd.last_op = 0;
    }

  /* Open file need to be close */
  if((pentry_file->object.file.open_fd.openflags != 0) &&
     (pentry_file->object.file.open_fd.fileno >= 0) &&
     (pentry_file->object.file.open_fd.openflags != openflags))
    {
      fsal_status = FSAL_close(&(pentry_file->object.file.open_fd.fd));
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          return *pstatus;
        }

    }

  if(pentry_file->object.file.open_fd.last_op == 0)
    {
      /* Keep coherency with the cache_content */
      if(pentry_file->object.file.pentry_content != NULL)
        {
          save_filesize = pentry_file->object.file.attributes.filesize;
          save_spaceused = pentry_file->object.file.attributes.spaceused;
          save_mtime = pentry_file->object.file.attributes.mtime;
        }

      /* opened file is not preserved yet */
      fsal_status = FSAL_open_by_name(&(pentry_dir->object.file.handle),
                                      pname,
                                      pcontext,
                                      openflags,
                                      &pentry_file->object.file.open_fd.fd,
                                      &(pentry_file->object.file.attributes));

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          return *pstatus;
        }
#ifdef _USE_PROXY

      /* If proxy if used, we should keep the name of the file to do FSAL_rcp if needed */
      if((pentry_file->object.file.pname =
          (fsal_name_t *) Mem_Alloc(sizeof(fsal_name_t))) == NULL)
        {
          *pstatus = CACHE_INODE_MALLOC_ERROR;

          return *pstatus;
        }

      pentry_file->object.file.pentry_parent_open = pentry_dir;
      pentry_file->object.file.pname->len = pname->len;
      memcpy((char *)(pentry_file->object.file.pname->name), (char *)(pname->name),
             FSAL_MAX_NAME_LEN);

#endif

      /* Keep coherency with the cache_content */
      if(pentry_file->object.file.pentry_content != NULL)
        {
          pentry_file->object.file.attributes.filesize = save_filesize;
          pentry_file->object.file.attributes.spaceused = save_spaceused;
          pentry_file->object.file.attributes.mtime = save_mtime;
        }

      pentry_file->object.file.open_fd.fileno =
          (int)FSAL_FILENO(&(pentry_file->object.file.open_fd.fd));
      pentry_file->object.file.open_fd.openflags = openflags;
    }

  /* regular exit */

  pentry_file->object.file.open_fd.last_op = time(NULL);
  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;

}                               /* cache_content_open_by_name */

/**
 *
 * cache_inode_close: closes the local fd in the FSAL.
 *
 * Closes the local fd in the FSAL.
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is
 * locked and will prevent from concurent accesses.
 *
 * @param pentry  [IN] entry in file content layer whose content is to be accessed.
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @pstatus       [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 */
cache_inode_status_t cache_inode_close(cache_entry_t * pentry,
                                       cache_inode_client_t * pclient,
                                       cache_inode_status_t * pstatus)
{
  fsal_status_t fsal_status;

  if((pentry == NULL) || (pclient == NULL) || (pstatus == NULL))
    return CACHE_CONTENT_INVALID_ARGUMENT;

  if(pentry->internal_md.type != REGULAR_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* if nothing is opened, do nothing */
  if(pentry->object.file.open_fd.fileno < 0)
    {
      *pstatus = CACHE_INODE_SUCCESS;
      return *pstatus;
    }

  if((pclient->use_cache == 0) ||
     (time(NULL) - pentry->object.file.open_fd.last_op > pclient->retention) ||
     (pentry->object.file.open_fd.fileno > (int)(pclient->max_fd_per_thread)))
    {
      pentry->object.file.open_fd.fileno = -1;
      pentry->object.file.open_fd.last_op = 0;

      fsal_status = FSAL_close(&(pentry->object.file.open_fd.fd));
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          return *pstatus;
        }
    }
#ifdef _USE_PROXY
  /* If proxy if used, free the name if needed */
  if(pentry->object.file.pname != NULL)
    {
      Mem_Free((char *)(pentry->object.file.pname));
      pentry->object.file.pname = NULL;
    }
  pentry->object.file.pentry_parent_open = NULL;
#endif

  *pstatus = CACHE_CONTENT_SUCCESS;

  return *pstatus;
}                               /* cache_content_close */
