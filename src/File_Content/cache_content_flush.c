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
 * \file    cache_content_init.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:33 $
 * \version $Revision: 1.8 $
 * \brief   Management of the file content cache: initialisation.
 *
 * cache_content.c : Management of the file content cache: initialisation.
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
#include <string.h>

/**
 *
 * cache_content_flush: Flushes the content of a file in the local cache to the FSAL data. 
 *
 * Flushes the content of a file in the local cache to the FSAL data. 
 * This routine should be called only from the cache_inode layer. 
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is 
 * locked and will prevent from concurent accesses.
 *
 * @param pentry   [IN]  entry in file content layer whose content is to be flushed.
 * @param flushhow [IN]  should we delete the cached entry in local or not ? 
 * @param pclient  [IN]  ressource allocated by the client for the nfs management.
 * @pstatus        [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 */
cache_content_status_t cache_content_flush(cache_content_entry_t * pentry,
                                           cache_content_flush_behaviour_t flushhow,
                                           cache_content_client_t * pclient,
                                           fsal_op_context_t * pcontext,
                                           cache_content_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  cache_inode_status_t cache_inode_status;
  fsal_path_t local_path;
  cache_entry_t *pentry_inode = NULL;

  /* Get the related cache inode entry */
  pentry_inode = (cache_entry_t *) pentry->pentry_inode;

  *pstatus = CACHE_CONTENT_SUCCESS;

  /* stat */
  pclient->stat.func_stats.nb_call[CACHE_CONTENT_FLUSH] += 1;

  /* Get the fsal handle */
  if ((pfsal_handle =
       cache_inode_get_fsal_handle(pentry->pentry_inode, &cache_inode_status)) == NULL)
    {
      *pstatus = CACHE_CONTENT_BAD_CACHE_INODE_ENTRY;

      DisplayLogJdLevel(pclient->log_outputs, NIV_MAJOR,
                        "cache_content_new_entry: cannot get handle");
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_FLUSH] += 1;

      return *pstatus;
    }

  /* Lock related Cache Inode pentry to avoid concurrency while read/write operation */
  P_w(&pentry->pentry_inode->lock);

  /* Convert the path to FSAL path */
  fsal_status =
      FSAL_str2path(pentry->local_fs_entry.cache_path_data, MAXPATHLEN, &local_path);

  if (FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = CACHE_CONTENT_FSAL_ERROR;

      /* Unlock related Cache Inode pentry */
      V_w(&pentry->pentry_inode->lock);

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_FLUSH] += 1;

      return *pstatus;
    }
#if ( defined( _USE_PROXY ) && defined( _BY_NAME) )
  fsal_status =
      FSAL_rcp_by_name(&
                       (pentry_inode->object.file.pentry_parent_open->object.dir_begin.
                        handle), pentry_inode->object.file.pname, pcontext, &local_path,
                       FSAL_RCP_LOCAL_TO_FS);
#else
  /* Write the data from the local data file to the fs file */
  fsal_status = FSAL_rcp(pfsal_handle, pcontext, &local_path, FSAL_RCP_LOCAL_TO_FS);
#endif

  if (FSAL_IS_ERROR(fsal_status))
    {
#if ( defined( _USE_PROXY ) && defined( _BY_NAME) )
      DisplayLogJdLevel(pclient->log_outputs, NIV_MAJOR,
                        "Error %d,%d from FSAL_rcp_by_name when flushing file",
                        fsal_status.major, fsal_status.minor);
#else
      DisplayLogJdLevel(pclient->log_outputs, NIV_MAJOR,
                        "Error %d,%d from FSAL_rcp when flushing file", fsal_status.major,
                        fsal_status.minor);
#endif

      /* Unlock related Cache Inode pentry */
      V_w(&pentry->pentry_inode->lock);

      *pstatus = CACHE_CONTENT_FSAL_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_FLUSH] += 1;

      return *pstatus;
    }

  /* To delete or not to delete ? That is the question ... */
  if (flushhow == CACHE_CONTENT_FLUSH_AND_DELETE)
    {
      /* Remove the index file from the data cache */
      if (unlink(pentry->local_fs_entry.cache_path_index))
        {
          /* Unlock related Cache Inode pentry */
          V_w(&pentry->pentry_inode->lock);

          DisplayLog("Can't unlink flushed index %s, errno=%u(%s)",
                     pentry->local_fs_entry.cache_path_index, errno, strerror(errno));
          *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
          return *pstatus;
        }

      /* Remove the data file from the data cache */
      if (unlink(pentry->local_fs_entry.cache_path_data))
        {
          /* Unlock related Cache Inode pentry */
          V_w(&pentry->pentry_inode->lock);

          DisplayLog("Can't unlink flushed index %s, errno=%u(%s)",
                     pentry->local_fs_entry.cache_path_data, errno, strerror(errno));
          *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;
          return *pstatus;
        }
    }

  /* Unlock related Cache Inode pentry */
  V_w(&pentry->pentry_inode->lock);

  /* Exit the function with no error */
  pclient->stat.func_stats.nb_success[CACHE_CONTENT_FLUSH] += 1;

  /* Update the internal metadata */
  pentry->internal_md.last_flush_time = time(NULL);
  pentry->local_fs_entry.sync_state = SYNC_OK;

  return *pstatus;
}                               /* cache_content_flush */

/**
 *
 * cache_content_refresh: Refreshes the whole content of a file in the local cache to the FSAL data. 
 *
 * Refreshes the whole content of a file in the local cache to the FSAL data.
 * This routine should be called only from the cache_inode layer. 
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is 
 * locked and will prevent from concurent accesses.
 *
* @param pentry [IN] entry in file content layer whose content is to be flushed.
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 * @todo: BUGAZOMEU: gestion de coherence de date a mettre en place
 */
cache_content_status_t cache_content_refresh(cache_content_entry_t * pentry,
                                             cache_content_client_t * pclient,
                                             fsal_op_context_t * pcontext,
                                             cache_content_refresh_how_t how,
                                             cache_content_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  cache_inode_status_t cache_inode_status;
  cache_entry_t *pentry_inode = NULL;
  fsal_path_t local_path;
  struct stat buffstat;

  *pstatus = CACHE_CONTENT_SUCCESS;

  /* stat */
  pclient->stat.func_stats.nb_call[CACHE_CONTENT_REFRESH] += 1;

  /* Get the related cache inode entry */
  pentry_inode = (cache_entry_t *) pentry->pentry_inode;

  /* Get the fsal handle */
  if ((pfsal_handle =
       cache_inode_get_fsal_handle(pentry_inode, &cache_inode_status)) == NULL)
    {
      *pstatus = CACHE_CONTENT_BAD_CACHE_INODE_ENTRY;

      DisplayLogJdLevel(pclient->log_outputs, NIV_MAJOR,
                        "cache_content_new_entry: cannot get handle");
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_REFRESH] += 1;

      return *pstatus;
    }

  /* Convert the path to FSAL path */
  fsal_status =
      FSAL_str2path(pentry->local_fs_entry.cache_path_data, MAXPATHLEN, &local_path);

  if (FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = CACHE_CONTENT_FSAL_ERROR;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_FLUSH] += 1;

      return *pstatus;
    }

  /* Stat the data file to check for incoherency (this can occur in a crash recovery context) */
  if (stat(pentry->local_fs_entry.cache_path_data, &buffstat) == -1)
    {
      *pstatus = CACHE_CONTENT_FSAL_ERROR;

      DisplayLogJdLevel(pclient->log_outputs, NIV_MAJOR,
                        "cache_content_new_entry: could'nt stat on %s, errno=%u(%s)",
                        pentry->local_fs_entry.cache_path_data, errno, strerror(errno));

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_FLUSH] += 1;

      return *pstatus;
    }
#ifdef _DEBUG_FSAL
  if (how == FORCE_FROM_FSAL)
    printf("FORCE FROM FSAL\n");
  else
    printf("FORCE FROM FSAL PAS ACTIVE\n");
#endif

  if ((how != FORCE_FROM_FSAL)
      && (buffstat.st_mtime >
          (time_t) pentry_inode->object.file.attributes.mtime.seconds))
    {
      *pstatus = CACHE_CONTENT_SUCCESS;

      DisplayLogJdLevel(pclient->log_outputs, NIV_DEBUG,
                        "Entry %s is more recent in data cache, keeping it");
      pentry_inode->object.file.attributes.mtime.seconds = buffstat.st_mtime;
      pentry_inode->object.file.attributes.mtime.nseconds = 0;
      pentry_inode->object.file.attributes.atime.seconds = buffstat.st_atime;
      pentry_inode->object.file.attributes.atime.nseconds = 0;
      pentry_inode->object.file.attributes.ctime.seconds = buffstat.st_ctime;
      pentry_inode->object.file.attributes.ctime.nseconds = 0;
    }
  else
    {
#if ( defined( _USE_PROXY ) && defined( _BY_NAME) )
      fsal_status =
          FSAL_rcp_by_name(&
                           (pentry_inode->object.file.pentry_parent_open->object.
                            dir_begin.handle), pentry_inode->object.file.pname, pcontext,
                           &local_path, FSAL_RCP_FS_TO_LOCAL);
#else
      /* Write the data from the local data file to the fs file */
      fsal_status = FSAL_rcp(pfsal_handle, pcontext, &local_path, FSAL_RCP_FS_TO_LOCAL);
#endif
      if (FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = CACHE_CONTENT_FSAL_ERROR;

#if ( defined( _USE_PROXY ) && defined( _BY_NAME) )
          DisplayLogJdLevel(pclient->log_outputs, NIV_MAJOR,
                            "FSAL_rcp_by_name failed for %s: fsal_status.major=%u fsal_status.minor=%u",
                            pentry->local_fs_entry.cache_path_data, fsal_status.major,
                            fsal_status.minor);
#else
          DisplayLogJdLevel(pclient->log_outputs, NIV_MAJOR,
                            "FSAL_rcp failed for %s: fsal_status.major=%u fsal_status.minor=%u",
                            pentry->local_fs_entry.cache_path_data, fsal_status.major,
                            fsal_status.minor);
#endif

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_REFRESH] += 1;

          return *pstatus;
        }

      /* Exit the function with no error */
      pclient->stat.func_stats.nb_success[CACHE_CONTENT_REFRESH] += 1;

      /* Update the internal metadata */
      pentry->internal_md.last_refresh_time = time(NULL);
      pentry->local_fs_entry.sync_state = SYNC_OK;

    }

  return *pstatus;
}                               /* cache_content_refresh */

cache_content_status_t cache_content_sync_all(cache_content_client_t * pclient,
                                              fsal_op_context_t * pcontext,
                                              cache_content_status_t * pstatus)
{
  *pstatus = CACHE_CONTENT_SUCCESS;
  return *pstatus;
}                               /* cache_content_sync_all */
