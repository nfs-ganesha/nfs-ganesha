/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
#include "log_functions.h"
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
          DisplayLogJdLevel(pclient_data->log_outputs, NIV_EVENT,
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
                  DisplayLogJdLevel(pclient_data->log_outputs, NIV_EVENT,
                                    "Cache entry for File ID %llx has been found", inum);

                  /* Get the content of the file */
                  sprintf(fullpath, "%s/%s/%s", pclient_data->cache_dir, direntp->d_name,
                          dirent_export.d_name);

                  if((cache_inode_status = cache_inode_reload_content(fullpath,
                                                                      &inode_entry)) !=
                     CACHE_INODE_SUCCESS)
                    {
                      DisplayLogJdLevel(pclient_data->log_outputs, NIV_MAJOR,
                                        "File Content Cache record for File ID %llx is unreadable",
                                        inum);
                      continue;
                    }
                  else
                    DisplayLogJdLevel(pclient_data->log_outputs, NIV_MAJOR,
                                      "File Content Cache record for File ID %llx : READ OK",
                                      inum);

                  /* Populating the cache_inode... */
                  fsal_data.handle = inode_entry.object.file.handle;
                  fsal_data.cookie = 0;

                  if((pentry = cache_inode_get(&fsal_data,
                                               &fsal_attr,
                                               ht,
                                               pclient_inode,
                                               pcontext, &cache_inode_status)) == NULL)
                    {
                      DisplayLogJd(pclient_inode->log_outputs,
                                   "Error adding cached inode for file ID %llx, error=%d",
                                   inum, cache_inode_status);
                      continue;
                    }
                  else
                    DisplayLogJdLevel(pclient_inode->log_outputs, NIV_EVENT,
                                      "Cached inode added successfully for file ID %llx",
                                      inum);

                  /* Get the size from the cache */
                  if((size_in_cache =
                      cache_content_recover_size(cache_exportdir, inum)) == -1)
                    {
                      DisplayLogJd(pclient_inode->log_outputs,
                                   "Error when recovering size for file ID %llx", inum);
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
                      DisplayLogJd(pclient_data->log_outputs,
                                   "Error adding cached data for file ID %llx, error=%d",
                                   inum, cache_inode_status);
                      continue;
                    }
                  else
                    DisplayLogJdLevel(pclient_data->log_outputs, NIV_EVENT,
                                      "Cached data added successfully for file ID %llx",
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
