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
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:46:35 $
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

#include "stuff_alloc.h"
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
 * cache_inode_init: Init the ressource necessary for the cache inode management.
 * 
 * Init the ressource necessary for the cache inode management.
 * 
 * @param param [IN] the parameter for this cache. 
 * @param pstatus [OUT] pointer to buffer used to store the status for the operation.
 *
 * @return 0 if operation failed, -1 if failed. 
 *
 */
int cache_content_init(cache_content_client_parameter_t param,
                       cache_content_status_t * pstatus)
{
  /* Try to create the cache directory */
  if (mkdir(param.cache_dir, 0750) != 0 && errno != EEXIST)
    {
      /* Cannot create the directory for caching data */
      fprintf(stderr, "Can't create cache dir = %s, error = ( %d, %s )\n",
              param.cache_dir, errno, strerror(errno));

      *pstatus = CACHE_CONTENT_INVALID_ARGUMENT;
      return -1;
    }

  /* Successfull exit */
  return 0;
}                               /* cache_content_init */

/**
 *
 * cache_content_init_dir: Init the directory for caching entries from a given export id. 
 *
 * @param param [IN] the parameter for this cache. 
 * @param export_id [IN] export id for the entries to be cached.
 * 
 * @return 0 if ok, -1 otherwise. Errno will be set with the error's value.
 *
 */
int cache_content_init_dir(cache_content_client_parameter_t param,
                           unsigned short export_id)
{
  char path_to_dir[MAXPATHLEN];

  snprintf(path_to_dir, MAXPATHLEN, "%s/export_id=%d", param.cache_dir, 0);

  if (mkdir(path_to_dir, 0750) != 0 && errno != EEXIST)
    {
      return -1;
    }

  return 0;
}                               /* cache_content_init_dir */

/**
 *
 * cache_content_client_init: Init the ressource necessary for the cache content client.
 *
 * Init the ressource necessary for the cache content client.
 *
 * @param param [IN] the parameter for this client
 * @param pstatus [OUT] pointer to buffer used to store the status for the operation.
 *
 * @return 0 if operation failed, -1 if failed.
 *
 */
int cache_content_client_init(cache_content_client_t * pclient,
                              cache_content_client_parameter_t param)
{
  LRU_status_t lru_status;

  pclient->log_outputs = param.log_outputs;
  pclient->nb_prealloc = param.nb_prealloc_entry;
  pclient->flush_force_fsal = param.flush_force_fsal;
  pclient->max_fd_per_thread = param.max_fd_per_thread;
  pclient->retention = param.retention;
  pclient->use_cache = param.use_cache;
  strncpy(pclient->cache_dir, param.cache_dir, MAXPATHLEN);

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("cache_content_entry_t");
#endif

#ifndef _NO_BLOCK_PREALLOC
  STUFF_PREALLOC(pclient->pool_entry,
                 pclient->nb_prealloc, cache_content_entry_t, next_alloc);

# ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("N/A");
# endif

  if (pclient->pool_entry == NULL)
    {
      DisplayLogJd(pclient->log_outputs,
                   "Error : can't init data_cache client entry pool");
      return 1;
    }
#endif

  /* Successfull exit */
  return 0;
}                               /* cache_content_init */
