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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <strings.h>

#ifdef _USE_MFSL
mfsl_file_t * cache_inode_fd(cache_entry_t * pentry)
#else
fsal_file_t * cache_inode_fd(cache_entry_t * pentry)
#endif
{
  if(pentry == NULL)
    return NULL;

  if(pentry->internal_md.type != REGULAR_FILE)
      return NULL;

  if(((pentry->object.file.open_fd.openflags == FSAL_O_RDONLY) ||
      (pentry->object.file.open_fd.openflags == FSAL_O_RDWR) ||
      (pentry->object.file.open_fd.openflags == FSAL_O_WRONLY)) &&
     (pentry->object.file.open_fd.fileno != 0))
    {
#ifdef _USE_MFSL
      return &pentry->object.file.open_fd.mfsl_fd;
#else
      return &pentry->object.file.open_fd.fd;
#endif
    }

  return NULL;
}

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

  /* Open file need to be closed, unless it is already open as read/write */
  if((pentry->object.file.open_fd.openflags != FSAL_O_RDWR) &&
     (pentry->object.file.open_fd.openflags != 0) &&
     (pentry->object.file.open_fd.fileno != 0) &&
     (pentry->object.file.open_fd.openflags != openflags))
    {
#ifdef _USE_MFSL
      fsal_status = MFSL_close(&(pentry->object.file.open_fd.mfsl_fd), &pclient->mfsl_context, NULL);
#else
      fsal_status = FSAL_close(&(pentry->object.file.open_fd.fd));
#endif
      if(FSAL_IS_ERROR(fsal_status) && (fsal_status.major != ERR_FSAL_NOT_OPENED))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_open: returning %d(%s) from FSAL_close",
                   *pstatus, cache_inode_err_str(*pstatus));

          return *pstatus;
        }

      /* force re-openning */
      pentry->object.file.open_fd.last_op = 0;
      pentry->object.file.open_fd.fileno = 0;

    }

  if((pentry->object.file.open_fd.last_op == 0)
     || (pentry->object.file.open_fd.fileno == 0))
    {
      /* opened file is not preserved yet */
#ifdef _USE_MFSL
      fsal_status = MFSL_open(&(pentry->mobject),
                              pcontext,
                              &pclient->mfsl_context,
                              openflags,
                              &pentry->object.file.open_fd.mfsl_fd,
                              &(pentry->object.file.attributes),
                              NULL );
#else
      fsal_status = FSAL_open(&(pentry->object.file.handle),
                              pcontext,
                              openflags,
                              &pentry->object.file.open_fd.fd,
                              &(pentry->object.file.attributes));
#endif

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_open: returning %d(%s) from FSAL_open",
                   *pstatus, cache_inode_err_str(*pstatus));

          return *pstatus;
        }

#ifdef _USE_MFSL
      pentry->object.file.open_fd.fileno = FSAL_FILENO(&(pentry->object.file.open_fd.mfsl_fd.fsal_file));
#else
      pentry->object.file.open_fd.fileno = FSAL_FILENO(&(pentry->object.file.open_fd.fd));
#endif
      pentry->object.file.open_fd.openflags = openflags;

      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_open: pentry %p: lastop=0, fileno = %d, openflags = %d",
               pentry, pentry->object.file.open_fd.fileno, (int) openflags);
    }

  /* regular exit */
  pentry->object.file.open_fd.last_op = time(NULL);

  /* if file descriptor is too high, garbage collect FDs */
  if(pclient->use_cache
     && (pentry->object.file.open_fd.fileno > pclient->max_fd_per_thread))
    {
      if(cache_inode_gc_fd(pclient, pstatus) != CACHE_INODE_SUCCESS)
        {
          LogCrit(COMPONENT_CACHE_INODE_GC,
                  "FAILURE performing FD garbage collection");
          return *pstatus;
        }
    }

  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;

}                               /* cache_inode_open */

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
  fsal_size_t save_filesize = 0;
  fsal_size_t save_spaceused = 0;
  fsal_time_t save_mtime = {
    .seconds = 0,
    .nseconds = 0
  };

  if((pentry_dir == NULL) || (pname == NULL) || (pentry_file == NULL) ||
     (pclient == NULL) || (pcontext == NULL) || (pstatus == NULL))
    return CACHE_INODE_INVALID_ARGUMENT;

  if((pentry_dir->internal_md.type != DIRECTORY))
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  if(pentry_file->internal_md.type != REGULAR_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* Open file need to be closed, unless it is already open as read/write */
  if((pentry_file->object.file.open_fd.openflags != FSAL_O_RDWR) &&
     (pentry_file->object.file.open_fd.openflags != 0) &&
     (pentry_file->object.file.open_fd.fileno >= 0) &&
     (pentry_file->object.file.open_fd.openflags != openflags))
    {
#ifdef _USE_MFSL
      fsal_status =
          MFSL_close(&(pentry_file->object.file.open_fd.mfsl_fd), &pclient->mfsl_context, NULL);
#else
      fsal_status = FSAL_close(&(pentry_file->object.file.open_fd.fd));
#endif
      if(FSAL_IS_ERROR(fsal_status) && (fsal_status.major != ERR_FSAL_NOT_OPENED))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_open_by_name: returning %d(%s) from FSAL_close",
                   *pstatus, cache_inode_err_str(*pstatus));

          return *pstatus;
        }

      pentry_file->object.file.open_fd.last_op = 0;
      pentry_file->object.file.open_fd.fileno = 0;
    }

  if(pentry_file->object.file.open_fd.last_op == 0
     || pentry_file->object.file.open_fd.fileno == 0)
    {
      LogDebug(COMPONENT_FSAL,
               "cache_inode_open_by_name: pentry %p: lastop=0", pentry_file);

      /* Keep coherency with the cache_content */
      if(pentry_file->object.file.pentry_content != NULL)
        {
          save_filesize = pentry_file->object.file.attributes.filesize;
          save_spaceused = pentry_file->object.file.attributes.spaceused;
          save_mtime = pentry_file->object.file.attributes.mtime;
        }

      /* opened file is not preserved yet */
#ifdef _USE_MFSL
      fsal_status = MFSL_open_by_name(&(pentry_dir->mobject),
                                      pname,
                                      pcontext,
                                      &pclient->mfsl_context,
                                      openflags,
                                      &pentry_file->object.file.open_fd.mfsl_fd,
                                      &(pentry_file->object.file.attributes),
#ifdef _USE_PNFS
                                      &pentry_file->object.file.pnfs_file ) ;
#else
                                      NULL );
#endif /* _USE_PNFS */

#else
      fsal_status = FSAL_open_by_name(&(pentry_dir->object.file.handle),
                                      pname,
                                      pcontext,
                                      openflags,
                                      &pentry_file->object.file.open_fd.fd,
                                      &(pentry_file->object.file.attributes));
#endif

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_open_by_name: returning %d(%s) from FSAL_open_by_name",
                   *pstatus, cache_inode_err_str(*pstatus));

          return *pstatus;
        }

#ifdef _USE_PROXY

      /* If proxy if used, we should keep the name of the file to do FSAL_rcp if needed */
      if((pentry_file->object.file.pname =
          (fsal_name_t *) Mem_Alloc_Label(sizeof(fsal_name_t), "fsal_name_t")) == NULL)
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

#ifdef _USE_MFSL
      pentry_file->object.file.open_fd.fileno =
          (int)FSAL_FILENO(&(pentry_file->object.file.open_fd.mfsl_fd.fsal_file));
#else
      pentry_file->object.file.open_fd.fileno =
          (int)FSAL_FILENO(&(pentry_file->object.file.open_fd.fd));
#endif
      pentry_file->object.file.open_fd.last_op = time(NULL);
      pentry_file->object.file.open_fd.openflags = openflags;

      LogDebug(COMPONENT_FSAL,
               "cache_inode_open_by_name: pentry %p: fd=%u",
               pentry_file, pentry_file->object.file.open_fd.fileno);

    }

  /* regular exit */
  pentry_file->object.file.open_fd.last_op = time(NULL);

  /* if file descriptor is too high, garbage collect FDs */
  if(pclient->use_cache
     && (pentry_file->object.file.open_fd.fileno > pclient->max_fd_per_thread))
    {
      if(cache_inode_gc_fd(pclient, pstatus) != CACHE_INODE_SUCCESS)
        {
          LogCrit(COMPONENT_CACHE_INODE_GC,
                  "FAILURE performing FD garbage collection");
          return *pstatus;
        }
    }

  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;

}                               /* cache_inode_open_by_name */

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

  /* if locks are held in the file, do not close */
  if( cache_inode_file_holds_state( pentry ) )
    {
      *pstatus = CACHE_INODE_SUCCESS; /** @todo : PhD : May be CACHE_INODE_STATE_CONFLICTS would be better ? */
      return *pstatus;
    }

  if((pclient->use_cache == 0) ||
     (time(NULL) - pentry->object.file.open_fd.last_op > pclient->retention) ||
     (pentry->object.file.open_fd.fileno > (int)(pclient->max_fd_per_thread)))
    {

      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_close: pentry %p, fileno = %d, lastop=%d ago",
               pentry, pentry->object.file.open_fd.fileno,
               (int)(time(NULL) - pentry->object.file.open_fd.last_op));

#ifdef _USE_MFSL
      fsal_status = MFSL_close(&(pentry->object.file.open_fd.mfsl_fd), &pclient->mfsl_context, NULL);
#else
      fsal_status = FSAL_close(&(pentry->object.file.open_fd.fd));
#endif

      pentry->object.file.open_fd.fileno = 0;
      pentry->object.file.open_fd.last_op = 0;

      if(FSAL_IS_ERROR(fsal_status) && (fsal_status.major != ERR_FSAL_NOT_OPENED))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_close: returning %d(%s) from FSAL_close",
                   *pstatus, cache_inode_err_str(*pstatus));

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
