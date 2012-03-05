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
 * \file    cache_content_CRASH_RECOVER.C
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:32 $
 * \version $Revision: 1.6 $
 * \brief   Management of the file content cache: crash recovery.
 *
 * cache_content_crash_recover.c : Management of the file content cache: crash recovery.
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
#include <errno.h>
#include <dirent.h>
#include <string.h>

/**
 *
 * cache_content_crash_recover: recovers the data cache and the associated inode after a crash.
 *
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful.
 *
 */
cache_content_status_t cache_content_crash_recover(unsigned short exportid,
                                                   unsigned int index,
                                                   unsigned int mod,
                                                   cache_content_client_t * pclient_data,
                                                   cache_inode_client_t * pclient_inode,
                                                   hash_table_t * ht,
                                                   fsal_op_context_t * pcontext,
                                                   cache_content_status_t * pstatus)
{
  DIR *cache_directory;
  cache_content_dirinfo_t export_directory;

  char cache_exportdir[MAXPATHLEN];
  char fullpath[MAXPATHLEN];

  struct dirent *direntp;
  struct dirent dirent_export;

  int found_export_id;
  u_int64_t inum;

  off_t size_in_cache;

  cache_entry_t inode_entry;
  cache_entry_t *pentry = NULL;
  cache_content_entry_t *pentry_content = NULL;
  cache_inode_status_t cache_inode_status;
  cache_content_status_t cache_content_status;

  fsal_attrib_list_t fsal_attr;
  cache_inode_fsal_data_t fsal_data;

  *pstatus = CACHE_CONTENT_SUCCESS;

  /* Open the cache directory */
  if((cache_directory = opendir(pclient_data->cache_dir)) == NULL)
    {
      *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
      return *pstatus;
    }
  /* read the cache directory */
  while((direntp = readdir(cache_directory)) != NULL)
    {

      /* . and .. are of no interest */
      if(!strcmp(direntp->d_name, ".") || !strcmp(direntp->d_name, ".."))
        continue;

      if((found_export_id = cache_content_get_export_id(direntp->d_name)) >= 0)
        {
          LogEvent(COMPONENT_CACHE_CONTENT,
                            "Directory cache for Export ID %d has been found",
                            found_export_id);
          snprintf(cache_exportdir, MAXPATHLEN, "%s/%s", pclient_data->cache_dir,
                   direntp->d_name);

          if(cache_content_local_cache_opendir(cache_exportdir, &(export_directory)) ==
             FALSE)
            {
              *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
              closedir(cache_directory);
              return *pstatus;
            }

          /* Reads the directory content (a single thread for the moment) */

          while(cache_content_local_cache_dir_iter
                (&export_directory, &dirent_export, index, mod))
            {
              /* . and .. are of no interest */
              if(!strcmp(dirent_export.d_name, ".")
                 || !strcmp(dirent_export.d_name, ".."))
                continue;

              if((inum = cache_content_get_inum(dirent_export.d_name)) > 0)
                {
                  LogEvent(COMPONENT_CACHE_CONTENT,
                                    "Cache entry for File ID %"PRIx64" has been found", inum);

                  /* Get the content of the file */
                  sprintf(fullpath, "%s/%s/%s", pclient_data->cache_dir, direntp->d_name,
                          dirent_export.d_name);

                  if((cache_inode_status = cache_inode_reload_content(fullpath,
                                                                      &inode_entry)) !=
                     CACHE_INODE_SUCCESS)
                    {
                      LogMajor(COMPONENT_CACHE_CONTENT,
                                        "File Content Cache record for File ID %"PRIx64" is unreadable",
                                        inum);
                      continue;
                    }
                  else
                    LogMajor(COMPONENT_CACHE_CONTENT,
                                      "File Content Cache record for File ID %"PRIx64" : READ OK",
                                      inum);

                  /* Populating the cache_inode... */
                  fsal_data.handle = inode_entry.object.file.handle;
                  fsal_data.cookie = 0;

                  if((pentry = cache_inode_get(&fsal_data,
                                               CACHE_INODE_JOKER_POLICY,
                                               &fsal_attr,
                                               ht,
                                               pclient_inode,
                                               pcontext, &cache_inode_status)) == NULL)
                    {
                      LogCrit(COMPONENT_CACHE_CONTENT,
                                   "Error adding cached inode for file ID %"PRIx64", error=%d",
                                   inum, cache_inode_status);
                      continue;
                    }
                  else
                    LogEvent(COMPONENT_CACHE_CONTENT,
                                      "Cached inode added successfully for file ID %"PRIx64,
                                      inum);

                  /* Get the size from the cache */
                  if((size_in_cache =
                      cache_content_recover_size(cache_exportdir, inum)) == -1)
                    {
                      LogCrit(COMPONENT_CACHE_CONTENT,
                                   "Error when recovering size for file ID %"PRIx64, inum);
                    }
                  else
                    pentry->object.file.attributes.filesize = (fsal_size_t) size_in_cache;

                  /* Adding the cached entry to the data cache */
                  if((pentry_content = cache_content_new_entry(pentry,
                                                               NULL,
                                                               pclient_data,
                                                               RECOVER_ENTRY,
                                                               pcontext,
                                                               &cache_content_status)) ==
                     NULL)
                    {
                      LogCrit(COMPONENT_CACHE_CONTENT,
                                   "Error adding cached data for file ID %"PRIx64", error=%d",
                                   inum, cache_inode_status);
                      continue;
                    }
                  else
                    LogEvent(COMPONENT_CACHE_CONTENT,
                                      "Cached data added successfully for file ID %"PRIx64,
                                      inum);

                  if((cache_content_status =
                      cache_content_valid(pentry_content, CACHE_CONTENT_OP_GET,
                                          pclient_data)) != CACHE_CONTENT_SUCCESS)
                    {
                      *pstatus = cache_content_status;
                      return *pstatus;
                    }

                }

            }                   /*  while( ( dirent_export = readdir( export_directory ) ) != NULL ) */

          /* Close the export cache directory */
          cache_content_local_cache_closedir(&export_directory);

        }                       /* if( ( found_export_id = cache_content_get_export_id( direntp->d_name ) ) > 0 ) */
    }                           /* while( ( direntp = readdir( cache_directory ) ) != NULL ) */

  /* Close the cache directory */
  closedir(cache_directory);

  return *pstatus;
}                               /* cache_content_crash_recover */
