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
 * \file    cache_content_rdwr.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:33 $
 * \version $Revision: 1.12 $
 * \brief   Management of the file content cache: read/write operations. 
 *
 * cache_content_rdwr.c.c : Management of the file content cache, read and write operations.
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/* Missing prototypes */

/**
 *
 * cache_content_open: opens the local fd on  the cache.
 *
 * Opens the local fd on  the cache.
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

cache_content_status_t cache_content_open(cache_content_entry_t * pentry,
                                          cache_content_client_t * pclient,
                                          cache_content_status_t * pstatus)
{
  int localfd;

  if((pentry == NULL) || (pstatus == NULL))
    return CACHE_CONTENT_INVALID_ARGUMENT;

  if(pclient->use_cache == 0)
    pentry->local_fs_entry.opened_file.last_op = 0;     /* to force opening the file */

  if((pclient->use_cache == 0) ||
     (time(NULL) - pentry->local_fs_entry.opened_file.last_op > pclient->retention))
    {

      if(pentry->local_fs_entry.opened_file.local_fd > 0)
        {
          close(pentry->local_fs_entry.opened_file.local_fd);
        }

      pentry->local_fs_entry.opened_file.local_fd = -1;
      pentry->local_fs_entry.opened_file.last_op = 0;
    }

  if(pentry->local_fs_entry.opened_file.last_op == 0)
    {
      /* Close file to be sure */
      if(pentry->local_fs_entry.opened_file.local_fd > 0)
        {
          close(pentry->local_fs_entry.opened_file.local_fd);
        }

      /* opened file is not preserved yet */
      if((localfd = open(pentry->local_fs_entry.cache_path_data, O_RDWR, 0750)) == -1)
        {
          if(errno == ENOENT)
            *pstatus = CACHE_CONTENT_LOCAL_CACHE_NOT_FOUND;
          else
            *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;

          return *pstatus;
        }

      pentry->local_fs_entry.opened_file.local_fd = localfd;
    }

  /* Regular exit */
  pentry->local_fs_entry.opened_file.last_op = time(NULL);

  *pstatus = CACHE_CONTENT_SUCCESS;
  return *pstatus;

}                               /* cache_content_open */

/**
 *
 * cache_content_close: closes the local fd on  the cache.
 *
 * Closes the local fd on  the cache.
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
cache_content_status_t cache_content_close(cache_content_entry_t * pentry,
                                           cache_content_client_t * pclient,
                                           cache_content_status_t * pstatus)
{
  if((pentry == NULL) || (pstatus == NULL))
    return CACHE_CONTENT_INVALID_ARGUMENT;

  /* if nothing is opened, do nothing */
  if(pentry->local_fs_entry.opened_file.local_fd < 0)
    {
      *pstatus = CACHE_CONTENT_SUCCESS;
      return *pstatus;
    }

  if((pclient->use_cache == 0) ||
     (time(NULL) - pentry->local_fs_entry.opened_file.last_op > pclient->retention) ||
     (pentry->local_fs_entry.opened_file.local_fd > pclient->max_fd_per_thread))
    {
      close(pentry->local_fs_entry.opened_file.local_fd);
      pentry->local_fs_entry.opened_file.local_fd = -1;
      pentry->local_fs_entry.opened_file.last_op = 0;
    }

  *pstatus = CACHE_CONTENT_SUCCESS;
  return *pstatus;
}                               /* cache_content_close */

/**
 *
 * cache_content_rdwr: Reads/Writes through the cache layer.
 *
 * Reads/Writes through the cache layer.
 * This routine should be called only from the cache_inode layer. 
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is 
 * locked and will prevent from concurent accesses.
 *
 * @param pentry          [IN] entry in file content layer whose content is to be accessed.
 * @param read_or_write   [IN] a flag of type cache_content_io_direction_t to tell if a read or write is to be done. 
 * @param seek_descriptor [IN] absolute position (in the FSAL file) where the IO will be done.
 * @param pio_size_in     [IN] requested io size
 * @param pio_size_out    [OUT] the size of the io that was successfully made.
 * @param pbuffstat       [OUT] the 'stat' of entry in the data cache after the operation
 * @param buffer write:[IN] read:[OUT] the buffer for the data.
 * @param pclient         [IN]  ressource allocated by the client for the nfs management.
 * @param pcontext        [IN] fsal credentials for the operation.
 * @pstatus               [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 */
cache_content_status_t cache_content_rdwr(cache_content_entry_t * pentry,
                                          cache_content_io_direction_t read_or_write,
                                          fsal_seek_t * seek_descriptor,
                                          fsal_size_t * pio_size_in,
                                          fsal_size_t * pio_size_out,
                                          caddr_t buffer,
                                          fsal_boolean_t * p_fsal_eof,
                                          struct stat * pbuffstat,
                                          cache_content_client_t * pclient,
                                          fsal_op_context_t * pcontext,
                                          cache_content_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  cache_inode_status_t cache_inode_status;
  cache_content_status_t cache_content_status;
  fsal_path_t local_path;
  int statindex;
  off_t offset;
  size_t iosize_before;
  ssize_t iosize_after;
  struct stat buffstat;
  int rc;
  char c;

  *pstatus = CACHE_CONTENT_SUCCESS;

  LogFullDebug(COMPONENT_CACHE_CONTENT,
                    "---> DATA : IO Size IN = %llu fdsize=%zu seeksize=%zu",
                    *pio_size_in, sizeof(fsal_file_t), sizeof(fsal_seek_t));

  /* For now, only FSAL_SEEK_SET is supported */
  if(seek_descriptor->whence != FSAL_SEEK_SET)
    {
      LogDebug(COMPONENT_CACHE_CONTENT,
                   "Implementation trouble: seek_descriptor was not a 'FSAL_SEEK_SET' cursor");
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Set the statindex variable */
  switch (read_or_write)
    {
    case CACHE_CONTENT_READ:
      statindex = CACHE_CONTENT_READ_ENTRY;
      break;

    case CACHE_CONTENT_WRITE:
      statindex = CACHE_CONTENT_WRITE_ENTRY;
      break;

    default:
      *pstatus = CACHE_CONTENT_INVALID_ARGUMENT;
      return *pstatus;
      break;
    }

  /* stat */
  pclient->stat.func_stats.nb_call[statindex] += 1;

  /* Get the fsal handle */
  if((pfsal_handle =
      cache_inode_get_fsal_handle(pentry->pentry_inode, &cache_inode_status)) == NULL)
    {
      *pstatus = CACHE_CONTENT_BAD_CACHE_INODE_ENTRY;

      LogMajor(COMPONENT_CACHE_CONTENT,
                        "cache_content_rdwr: cannot get handle");
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

      return *pstatus;
    }

  /* Convert the path to FSAL path */
  fsal_status =
      FSAL_str2path(pentry->local_fs_entry.cache_path_data, MAXPATHLEN, &local_path);

  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = CACHE_CONTENT_FSAL_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

      return *pstatus;
    }

  /* Parameters conversion */
  offset = cache_content_fsal_seek_convert(*seek_descriptor, pstatus);
  if(*pstatus != CACHE_CONTENT_SUCCESS)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

      return *pstatus;
    }

  iosize_before = cache_content_fsal_size_convert(*pio_size_in, pstatus);
  if(*pstatus != CACHE_CONTENT_SUCCESS)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

      return *pstatus;
    }

  /* Open the local fd for reading */
  if(cache_content_open(pentry, pclient, pstatus) != CACHE_CONTENT_SUCCESS)
    {
      return *pstatus;
    }

  /* Perform the IO through the cache */
  if(read_or_write == CACHE_CONTENT_READ)
    {
      /* The file content was completely read before the IO. The read operation is fully done locally */
      if((iosize_after =
          pread(pentry->local_fs_entry.opened_file.local_fd, buffer, iosize_before,
                offset)) == -1)
        {
          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

          *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
          return *pstatus;
        }

      if((cache_content_status =
          cache_content_valid(pentry, CACHE_CONTENT_OP_GET,
                              pclient)) != CACHE_CONTENT_SUCCESS)
        {
          *pstatus = cache_content_status;
          return *pstatus;
        }

      /* Get the eof */
      if(iosize_after == 0)
        *p_fsal_eof = TRUE;
      else
        {
          rc = pread(pentry->local_fs_entry.opened_file.local_fd, &c, 1,
                     offset + iosize_before);
          if(rc == 0)
            *p_fsal_eof = TRUE;
          else
            *p_fsal_eof = FALSE;
        }
    }
  else
    {
      /* The io is done on the cache before being flushed to the FSAL */
      if((iosize_after =
          pwrite(pentry->local_fs_entry.opened_file.local_fd, buffer, iosize_before,
                 offset)) == -1)
        {
          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

          *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
          return *pstatus;
        }

      if((cache_content_status =
          cache_content_valid(pentry, CACHE_CONTENT_OP_SET,
                              pclient)) != CACHE_CONTENT_SUCCESS)
        {
          *pstatus = cache_content_status;
          return *pstatus;
        }

      /* p_fsal_eof has no meaning here, it is unused */
    }

  /* close the local fd */
  if(cache_content_close(pentry, pclient, pstatus) != CACHE_CONTENT_SUCCESS)
    return *pstatus;

  *pio_size_out = (fsal_size_t) iosize_after;

  /* Return the 'stat' as seen in the cache */
  if(stat(pentry->local_fs_entry.cache_path_data, &buffstat) == -1)
    {
      *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
    }
  else
    {
      if(pbuffstat != NULL)
        *pbuffstat = buffstat;
    }

  return *pstatus;
}                               /* cache_content_rdwr */
