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
 * \file    cache_content_add_entry.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:32 $
 * \version $Revision: 1.12 $
 * \brief   Management of the file content cache: adding a new entry.
 *
 * cache_content_add_entry.c : Management of the file content cache: adding a new entry.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif				/* _SOLARIS */

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

/**
 *
 * cache_content_new_entry: adds an entry to the file content cache.
 *
 * Adds an entry to the file content cache.
 * This routine should be called only from the cache_inode layer. 
 *
 * No lock management is done in this layer: the related pentry in the cache inode layer is 
 * locked and will prevent from concurent accesses.
 *
 * @param pentry_inode [IN] entry in cache_inode layer for this file.
 * @param pspecdata [IN] pointer to the entry's specific data
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 */
cache_content_entry_t *cache_content_new_entry(cache_entry_t * pentry_inode,
					       cache_content_spec_data_t * pspecdata,
					       cache_content_client_t * pclient,
					       cache_content_add_behaviour_t how,
					       fsal_op_context_t * pcontext,
					       cache_content_status_t * pstatus)
{
  cache_content_status_t status;
  cache_content_entry_t *pfc_pentry = NULL;
  int tmpfd;

  /* Set the return default to CACHE_CONTENT_SUCCESS */
  *pstatus = CACHE_CONTENT_SUCCESS;

  /* stat */
  pclient->stat.func_stats.nb_call[CACHE_CONTENT_NEW_ENTRY] += 1;

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("cache_content_entry_t");
#endif

  if (pentry_inode == NULL)
    {
      *pstatus = CACHE_CONTENT_INVALID_ARGUMENT;

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

      return NULL;
    }

  if (how != RENEW_ENTRY)
    {
      /* Get the entry from the preallocated pool */
      GET_PREALLOC(pfc_pentry,
		   pclient->pool_entry,
		   pclient->nb_prealloc, cache_content_entry_t, next_alloc);

#ifdef _DEBUG_MEMLEAKS
      /* For debugging memory leaks */
      BuddySetDebugLabel("N/A");
#endif

      if (pfc_pentry == NULL)
	{
	  *pstatus = CACHE_CONTENT_MALLOC_ERROR;

	  DisplayLogJdLevel(pclient->log_outputs, NIV_DEBUG,
			    "cache_content_new_entry: can't allocate a new fc_entry from cache pool");

	  /* stat */
	  pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

	  return NULL;
	}
    } /* if( how != RENEW_ENTRY ) */
    else
    {
      /* When renewing a file content entry, pentry_content already exists in pentry_inode, just use it */
      pfc_pentry = (cache_content_entry_t *) (pentry_inode->object.file.pentry_content);
    }

  /* Set the path to the local files */
  if ((status = cache_content_create_name(pfc_pentry->local_fs_entry.cache_path_index,
					  CACHE_CONTENT_INDEX_FILE,
					  pcontext,
					  pentry_inode,
					  pclient)) != CACHE_CONTENT_SUCCESS)
    {
      RELEASE_PREALLOC(pfc_pentry, pclient->pool_entry, next_alloc);

      *pstatus = CACHE_CONTENT_ENTRY_EXISTS;

      /* stat */
      pclient->stat.func_stats.nb_err_retryable[CACHE_CONTENT_NEW_ENTRY] += 1;

      DisplayLogJdLevel(pclient->log_outputs, NIV_EVENT,
			"cache_content_new_entry: entry's index pathname could not be created");

      return NULL;
    }

  if ((status = cache_content_create_name(pfc_pentry->local_fs_entry.cache_path_data,
					  CACHE_CONTENT_DATA_FILE,
					  pcontext,
					  pentry_inode,
					  pclient)) != CACHE_CONTENT_SUCCESS)
    {
      RELEASE_PREALLOC(pfc_pentry, pclient->pool_entry, next_alloc);

      *pstatus = CACHE_CONTENT_ENTRY_EXISTS;

      /* stat */
      pclient->stat.func_stats.nb_err_retryable[CACHE_CONTENT_NEW_ENTRY] += 1;

      DisplayLogJdLevel(pclient->log_outputs, NIV_EVENT,
			"cache_content_new_entry: entry's data  pathname could not be created");

      return NULL;
    }

  DisplayLogJdLevel(pclient->log_outputs, NIV_DEBUG,
		    "added file content cache entry: Data=%s Index=%s",
		    pfc_pentry->local_fs_entry.cache_path_data,
		    pfc_pentry->local_fs_entry.cache_path_index);

  /* Set the sync state */
  pfc_pentry->local_fs_entry.sync_state = JUST_CREATED;

  /* Set the internal_md */
  pfc_pentry->internal_md.read_time = 0;
  pfc_pentry->internal_md.mod_time = 0;
  pfc_pentry->internal_md.refresh_time = 0;
  pfc_pentry->internal_md.alloc_time = time(NULL);
  pfc_pentry->internal_md.last_flush_time = 0;
  pfc_pentry->internal_md.last_refresh_time = 0;
  pfc_pentry->internal_md.valid_state = STATE_OK;

  /* Set the local fd info */
  pfc_pentry->local_fs_entry.opened_file.local_fd = -1;
  pfc_pentry->local_fs_entry.opened_file.last_op = 0;

  /* Dump the inode entry to the index file */
  if (cache_inode_dump_content(pfc_pentry->local_fs_entry.cache_path_index, pentry_inode)
      != CACHE_INODE_SUCCESS)
    {

      RELEASE_PREALLOC(pfc_pentry, pclient->pool_entry, next_alloc);

      *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;

      DisplayLogJdLevel(pclient->log_outputs, NIV_EVENT,
			"cache_content_new_entry: entry could not be dumped in file");

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

      return NULL;
    }

  /* Create the data file if entry is not recoverd (in this case, it already exists) */
  if (how == ADD_ENTRY || how == RENEW_ENTRY)
    {
      if ((tmpfd = creat(pfc_pentry->local_fs_entry.cache_path_data, 0750)) == -1)
	{
	  RELEASE_PREALLOC(pfc_pentry, pclient->pool_entry, next_alloc);

	  *pstatus = CACHE_CONTENT_LOCAL_CACHE_ERROR;

	  DisplayLogJdLevel(pclient->log_outputs, NIV_EVENT,
			    "cache_content_new_entry: data cache file could not be created, errno=%d (%s)",
			    errno, strerror(errno));

	  /* stat */
	  pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

	  return NULL;
	}

      /* Close the new fd */
      close(tmpfd);

    }

  /* if( how == ADD_ENTRY || how == RENEW_ENTRY ) */
  /* Cache the data from FSAL if there are some */
  /* Add the entry to the related cache inode entry */
  pentry_inode->object.file.pentry_content = pfc_pentry;
  pfc_pentry->pentry_inode = pentry_inode;

  /* Data cache is considered as more pertinent as data below in case of crash recovery */
  if (how != RECOVER_ENTRY)
    {
      /* Get the file content from the FSAL, populate the data cache */
      if (pclient->flush_force_fsal == 0)
	cache_content_refresh(pfc_pentry, pclient, pcontext, DEFAULT_REFRESH, &status);
	else
	cache_content_refresh(pfc_pentry, pclient, pcontext, FORCE_FROM_FSAL, &status);

      if (status != CACHE_CONTENT_SUCCESS)
	{
	  RELEASE_PREALLOC(pfc_pentry, pclient->pool_entry, next_alloc);

	  *pstatus = status;

	  DisplayLogJdLevel(pclient->log_outputs, NIV_EVENT,
			    "cache_content_new_entry: data cache file could not read from FSAL, status=%u",
			    status);

	  /* stat */
	  pclient->stat.func_stats.nb_err_unrecover[CACHE_CONTENT_NEW_ENTRY] += 1;

	  return NULL;
	}
    }
  /* if ( how != RECOVER_ENTRY ) */
  return pfc_pentry;
}				/* cache_content_new_entry */
