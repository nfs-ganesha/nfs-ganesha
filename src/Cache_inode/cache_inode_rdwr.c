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
 * \file    cache_inode_rdwr.c
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
#include "nfs_core.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_rdwr: Reads/Writes through the cache layer.
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
 * @param stable[IN] if FALSE, data will be written to unstable storage (for implementing write/commit)
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 * @todo: BUGAZEOMEU; gestion de la taille du fichier a prendre en compte.
 *
 */

cache_inode_status_t cache_inode_rdwr(cache_entry_t * pentry,
                                      cache_inode_io_direction_t read_or_write,
                                      fsal_seek_t * seek_descriptor,
                                      fsal_size_t buffer_size,
                                      fsal_size_t * pio_size,
                                      fsal_attrib_list_t * pfsal_attr,
                                      caddr_t buffer,
                                      fsal_boolean_t * p_fsal_eof,
                                      hash_table_t * ht,
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t * pcontext,
                                      uint64_t stable, 
				      cache_inode_status_t * pstatus)
{
  int statindex = 0;
  cache_content_io_direction_t io_direction;
  cache_content_status_t cache_content_status;
  fsal_status_t fsal_status;
  fsal_openflags_t openflags;
  fsal_size_t io_size;
  fsal_attrib_list_t post_write_attr;
  fsal_status_t fsal_status_getattr;
  struct stat buffstat;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* For now, only FSAL_SEEK_SET is supported */
  if(seek_descriptor->whence != FSAL_SEEK_SET)
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "Implementation trouble: seek_descriptor was not a 'FSAL_SEEK_SET' cursor");
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  io_size = buffer_size;

  LogDebug(COMPONENT_CACHE_INODE,
           "cache_inode_rdwr: INODE : IO Size = %llu fdsize =%zu seeksize=%zu",
           buffer_size, sizeof(fsal_file_t), sizeof(fsal_seek_t));

  /* stat */
  pclient->stat.nb_call_total += 1;
  if(read_or_write == CACHE_INODE_READ)
    {
      statindex = CACHE_INODE_READ_DATA;
      io_direction = CACHE_CONTENT_READ;
      openflags = FSAL_O_RDONLY;
      pclient->stat.func_stats.nb_call[CACHE_INODE_READ_DATA] += 1;
    }
  else
    {
      statindex = CACHE_INODE_WRITE_DATA;
      io_direction = CACHE_CONTENT_WRITE;
      openflags = FSAL_O_WRONLY;
      pclient->stat.func_stats.nb_call[CACHE_INODE_WRITE_DATA] += 1;
    }

  P_w(&pentry->lock);

  /* IO are done only on REGULAR_FILEs */
  if(pentry->internal_md.type != REGULAR_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      V_w(&pentry->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

      return *pstatus;
    }

  /* Non absolute address within the file are not supported (we act only like pread/pwrite) */
  if(seek_descriptor->whence != FSAL_SEEK_SET)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      V_w(&pentry->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

      return *pstatus;
    }

  /* Do we use stable or unstable storage ? */
  if(stable == FSAL_UNSAFE_WRITE_TO_GANESHA_BUFFER)
    {
      /* Data will be stored in memory and not flush to FSAL */

      /* If the unstable_data buffer allocated ? */
      if(pentry->object.file.unstable_data.buffer == NULL)
        {
          if((pentry->object.file.unstable_data.buffer =
              Mem_Alloc_Label(CACHE_INODE_UNSTABLE_BUFFERSIZE,
                              "Cache_Inode Unstable Buffer")) == NULL)
            {
              *pstatus = CACHE_INODE_MALLOC_ERROR;
              V_w(&pentry->lock);

              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

              return *pstatus;
            }

          pentry->object.file.unstable_data.offset = seek_descriptor->offset;
          pentry->object.file.unstable_data.length = buffer_size;

          memcpy(pentry->object.file.unstable_data.buffer, buffer, buffer_size);

          /* Set mtime and ctime */
          cache_inode_set_time_current( &pentry->object.file.attributes.mtime ) ;  

          /* BUGAZOMEU : write operation must NOT modify file's ctime */
          pentry->object.file.attributes.ctime = pentry->object.file.attributes.mtime;

          *pio_size = buffer_size;
        }                       /* if( pentry->object.file.unstable_data.buffer == NULL ) */
      else
        {
          if((pentry->object.file.unstable_data.offset < seek_descriptor->offset) &&
             (buffer_size + seek_descriptor->offset < CACHE_INODE_UNSTABLE_BUFFERSIZE))
            {
              pentry->object.file.unstable_data.length =
                  buffer_size + seek_descriptor->offset;
              memcpy((char *)(pentry->object.file.unstable_data.buffer +
                              seek_descriptor->offset), buffer, buffer_size);

              /* Set mtime and ctime */
              cache_inode_set_time_current( &pentry->object.file.attributes.mtime ) ;

              /* BUGAZOMEU : write operation must NOT modify file's ctime */
              pentry->object.file.attributes.ctime = pentry->object.file.attributes.mtime;

              *pio_size = buffer_size;
            }
          else
            {
              /* Go back to regular situation */
              stable = FSAL_SAFE_WRITE_TO_FS;
            }
        }

    }
  /* if( stable == FALSE ) */
  if(stable == FSAL_SAFE_WRITE_TO_FS ||
     stable == FSAL_UNSAFE_WRITE_TO_FS_BUFFER)
    {
      /* Calls file content cache to operate on the cache */
      if(pentry->object.file.pentry_content != NULL)
        {
          /* Entry is data cached */
          cache_content_rdwr(pentry->object.file.pentry_content,
                             io_direction,
                             seek_descriptor,
                             &io_size,
                             pio_size,
                             buffer,
                             p_fsal_eof,
                             &buffstat,
                             (cache_content_client_t *) pclient->pcontent_client,
                             pcontext, &cache_content_status);

          /* If the entry under resync */
          if(cache_content_status == CACHE_CONTENT_LOCAL_CACHE_NOT_FOUND)
            {
              /* Data cache gc has removed this entry */
              if(cache_content_new_entry(pentry,
                                         NULL,
                                         (cache_content_client_t *)pclient->pcontent_client, 
                                         RENEW_ENTRY, pcontext,
                                         &cache_content_status) == NULL)
                {
                  /* Entry could not be recoverd, cache_content_status contains an error, let it be managed by the next block */
                  LogCrit(COMPONENT_CACHE_INODE,
                          "Read/Write Operation through cache failed with status %d (renew process failed)",
                          cache_content_status);

		  /* Will go to the end of the function on the error clause with cache_content_status describing the error */
                }
              else
                {
                  /* Entry was successfully renewed */
                  LogInfo(COMPONENT_CACHE_INODE,
                          "----> File Content Entry %p was successfully renewed",
                          pentry);

                  /* Try to access the content of the file again */
                  cache_content_rdwr(pentry->object.file.pentry_content,
                                     io_direction,
                                     seek_descriptor,
                                     &io_size,
                                     pio_size,
                                     buffer,
                                     p_fsal_eof,
                                     &buffstat,
                                     (cache_content_client_t *) pclient->pcontent_client,
                                     pcontext, &cache_content_status);

                  /* No management of cache_content_status in case of failure, this will be done
                   * within the next block */
                }

            }

          if(cache_content_status != CACHE_CONTENT_SUCCESS)
            {
              *pstatus = cache_content_error_convert(cache_content_status);

              V_w(&pentry->lock);

              LogCrit(COMPONENT_CACHE_INODE,
                      "Read/Write Operation through cache failed with status %d",
                      cache_content_status);

              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

              return *pstatus;
            }

          LogFullDebug(COMPONENT_CACHE_INODE,
                       "cache_inode_rdwr: inode/dc: io_size=%llu, pio_size=%llu,  eof=%d, seek=%d.%"PRIu64,
                       io_size, *pio_size, *p_fsal_eof, seek_descriptor->whence,
                       seek_descriptor->offset);

          LogFullDebug(COMPONENT_CACHE_INODE,
                       "cache_inode_rdwr: INODE  AFTER : IO Size = %llu %llu",
                       io_size, *pio_size);

          /* Use information from the buffstat to update the file metadata */
          pentry->object.file.attributes.filesize = buffstat.st_size;
          pentry->object.file.attributes.spaceused =
              buffstat.st_blksize * buffstat.st_blocks;

        }
      else
        {
          /* No data cache entry, we operated directly on FSAL */
          pentry->object.file.attributes.asked_attributes = pclient->attrmask;

          /* We need to open if we don't have a cached
           * descriptor or our open flags differs.
           */
          if(cache_inode_open(pentry,
                              pclient,
                              openflags, pcontext, pstatus) != CACHE_INODE_SUCCESS)
            {
              V_w(&pentry->lock);

              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

              return *pstatus;
            }

          /* Call FSAL_read or FSAL_write */

          if(read_or_write == CACHE_INODE_READ)
            {
#ifdef _USE_MFSL
              fsal_status = MFSL_read(&(pentry->object.file.open_fd.mfsl_fd),
                                      seek_descriptor,
                                      io_size,
                                      buffer,
                                      pio_size, p_fsal_eof, &pclient->mfsl_context, NULL);
#else
              fsal_status = FSAL_read(&(pentry->object.file.open_fd.fd),
                                      seek_descriptor,
                                      io_size, buffer, pio_size, p_fsal_eof);
#endif
            }
          else
            {
#ifdef _USE_MFSL
              fsal_status = MFSL_write(&(pentry->object.file.open_fd.mfsl_fd),
                                       seek_descriptor,
                                       io_size, buffer, pio_size, &pclient->mfsl_context, NULL);
#else
              fsal_status = FSAL_write(&(pentry->object.file.open_fd.fd),
                                       seek_descriptor, io_size, buffer, pio_size);
#endif

#if 0
              /* Alright, the unstable write is complete. Now if it was supposed to be a stable write
               * we can sync to the hard drive. */
              if(stable == FSAL_SAFE_WRITE_TO_FS)
                {
#ifdef _USE_MFSL
                  fsal_status = MFSL_sync(&(pentry->object.file.open_fd.mfsl_fd), NULL);
#else
                  fsal_status = FSAL_sync(&(pentry->object.file.open_fd.fd));
#endif
#endif

            }

          LogFullDebug(COMPONENT_FSAL,
                       "cache_inode_rdwr: FSAL IO operation returned %d, asked_size=%llu, effective_size=%llu",
                       fsal_status.major, (unsigned long long)io_size,
                       (unsigned long long)*pio_size);

          if(FSAL_IS_ERROR(fsal_status))
            {

              if(fsal_status.major == ERR_FSAL_DELAY)
                LogEvent(COMPONENT_CACHE_INODE,
                         "cache_inode_rdwr: FSAL_write returned EBUSY");
              else
                LogDebug(COMPONENT_CACHE_INODE,
                         "cache_inode_rdwr: fsal_status.major = %d",
                         fsal_status.major);

              if((fsal_status.major != ERR_FSAL_NOT_OPENED)
                 && (pentry->object.file.open_fd.fileno != 0))
                {

                  LogFullDebug(COMPONENT_CACHE_INODE,
                               "cache_inode_rdwr: CLOSING pentry %p: fd=%d",
                               pentry, pentry->object.file.open_fd.fileno);

#ifdef _USE_MFSL
                  MFSL_close(&(pentry->object.file.open_fd.mfsl_fd), &pclient->mfsl_context, NULL);
#else
                  FSAL_close(&(pentry->object.file.open_fd.fd));
#endif

                  *pstatus = cache_inode_error_convert(fsal_status);
                }
              else
                {
                  /* the fd has been close by another thread.
                   * return CACHE_INODE_FSAL_DELAY so the client will
                   * retry with a new fd.
                   */
                  *pstatus = CACHE_INODE_FSAL_DELAY;
                }

              pentry->object.file.open_fd.last_op = 0;
              pentry->object.file.open_fd.fileno = 0;

              V_w(&pentry->lock);

              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

              return *pstatus;
            }

          LogFullDebug(COMPONENT_CACHE_INODE,
                       "cache_inode_rdwr: inode/direct: io_size=%llu, pio_size=%llu, eof=%d, seek=%d.%"PRIu64,
                       io_size, *pio_size, *p_fsal_eof, seek_descriptor->whence,
                       seek_descriptor->offset);

          if(cache_inode_close(pentry, pclient, pstatus) != CACHE_INODE_SUCCESS)
            {
              LogEvent(COMPONENT_CACHE_INODE,
                       "cache_inode_rdwr: cache_inode_close = %d", *pstatus);

              V_w(&pentry->lock);

              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[statindex] += 1;

              return *pstatus;
            }

          if(read_or_write == CACHE_INODE_WRITE)
            {
              /* Do a getattr in order to have update information on filesize
               * This query is done directly on FSAL (object is not data cached), and result
               * will be propagated to cache Inode */

              /* WARNING: This operation is to be done AFTER FSAL_close (some FSAL, like POSIX,
               * may not flush data until the file is closed */

              /*post_write_attr.asked_attributes =  pclient->attrmask ; */
              post_write_attr.asked_attributes = FSAL_ATTR_SIZE | FSAL_ATTR_SPACEUSED;
              fsal_status_getattr =
                  FSAL_getattrs(&(pentry->object.file.handle), pcontext,
                                &post_write_attr);

              /* if failed, the next block will handle the error */
              if(FSAL_IS_ERROR(fsal_status_getattr))
                fsal_status = fsal_status_getattr;
              else
                {
                  /* Update Cache Inode attributes */
                  pentry->object.file.attributes.filesize = post_write_attr.filesize;
                  pentry->object.file.attributes.spaceused = post_write_attr.spaceused;
                }
            }

        }

      /* IO was successfull (through cache content or not), we manually update the times in the attributes */

      switch (read_or_write)
        {
        case CACHE_INODE_READ:
          /* Set the atime */
          cache_inode_set_time_current( & pentry->object.file.attributes.atime ) ;
          break;

        case CACHE_INODE_WRITE:
          /* Set mtime and ctime */
          cache_inode_set_time_current( & pentry->object.file.attributes.mtime ) ;

          /* BUGAZOMEU : write operation must NOT modify file's ctime */
          pentry->object.file.attributes.ctime = pentry->object.file.attributes.mtime;

          break;
        }
    }

  /* if(stable == TRUE ) */
  /* Return attributes to caller */
  if(pfsal_attr != NULL)
    *pfsal_attr = pentry->object.file.attributes;

  *pstatus = CACHE_INODE_SUCCESS;

  /* stat */
  if(read_or_write == CACHE_INODE_READ)
    {
      *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_GET, pclient);

      if(*pstatus != CACHE_INODE_SUCCESS)
        pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READ] += 1;
      else
        pclient->stat.func_stats.nb_success[CACHE_INODE_READ] += 1;
    }
  else
    {
      *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_SET, pclient);

      if(*pstatus != CACHE_INODE_SUCCESS)
        pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_WRITE] += 1;
      else
        pclient->stat.func_stats.nb_success[CACHE_INODE_WRITE] += 1;
    }

  V_w(&pentry->lock);

  return *pstatus;
}                               /* cache_inode_rdwr */
