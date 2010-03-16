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
 * \file    cache_inode_lookupp.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.5 $
 * \brief   Perform lookup through the cache to get the parent entry for a directory.
 *
 * cache_inode_lookupp.c : Perform lookup through the cache to get the parent entry for a directory.
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
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_lookupp_sw: looks up (and caches) the parent directory for a directory. A switches tells is mutex are use.
 * 
 * Looks up (and caches) the parent directory for a directory.
 *
 * @param pentry [IN] entry whose parent is to be obtained.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * @param use_mutex [IN] if TRUE mutex are use, not otherwise.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookupp_sw(cache_entry_t * pentry,
                                      hash_table_t * ht,
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t * pcontext,
                                      cache_inode_status_t * pstatus, int use_mutex)
{
  cache_entry_t *pentry_parent = NULL;
  fsal_status_t fsal_status;
  fsal_attrib_list_t object_attributes;
  cache_inode_fsal_data_t fsdata;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_LOOKUP] += 1;

  /* The entry should be a directory */
  if (use_mutex)
    P_r(&pentry->lock);
  if (pentry->internal_md.type != DIR_BEGINNING)
    {
      if (use_mutex)
        V_r(&pentry->lock);
      *pstatus = CACHE_INODE_BAD_TYPE;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUPP] += 1;

      return NULL;
    }

  /* Renew the entry (to avoid having it being garbagged */
  if (cache_inode_renew_entry(pentry, NULL, ht, pclient, pcontext, pstatus) !=
      CACHE_INODE_SUCCESS)
    {
      pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_GETATTR] += 1;
      return NULL;
    }

  /* Does the parent belongs to the cache ? */
  if (pentry->parent_list && pentry->parent_list->parent)
    {
      /* YES, the parent is cached, use the pentry that we have found */
      pentry_parent = pentry->parent_list->parent;
    } else
    {
      /* NO, the parent is not cached, query FSAL to get it and cache the result */
      object_attributes.asked_attributes = pclient->attrmask;
      fsal_status =
          FSAL_lookup(&pentry->object.dir_begin.handle, &FSAL_DOT_DOT, pcontext,
                      &fsdata.handle, &object_attributes);

      if (FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          if (use_mutex)
            V_r(&pentry->lock);

          /* Stale File Handle to be detected and managed */
          if (fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              DisplayLog("cache_inode_lookupp: Stale FSAL FH detected for pentry %p",
                         pentry);

              if (cache_inode_kill_entry(pentry, ht, pclient, &kill_status) !=
                  CACHE_INODE_SUCCESS)
                DisplayLog("cache_inode_remove: Could not kill entry %p, status = %u",
                           pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUPP] += 1;

          return NULL;
        }

      /* Call cache_inode_get to populate the cache with the parent entry */
      fsdata.cookie = 0;

      if ((pentry_parent = cache_inode_get(&fsdata,
                                           &object_attributes,
                                           ht, pclient, pcontext, pstatus)) == NULL)
        {
          if (use_mutex)
            V_r(&pentry->lock);

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUPP] += 1;

          return NULL;
        }
    }

  *pstatus = cache_inode_valid(pentry_parent, CACHE_INODE_OP_GET, pclient);
  if (use_mutex)
    V_r(&pentry->lock);

  /* stat */
  if (*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_LOOKUPP] += 1;
    else
    pclient->stat.func_stats.nb_success[CACHE_INODE_LOOKUPP] += 1;

  return pentry_parent;
}                               /* cache_inode_lookupp_sw */

/**
 *
 * cache_inode_lookupp: looks up (and caches) the parent directory for a directory.
 * 
 * Looks up (and caches) the parent directory for a directory.
 *
 * @param pentry [IN] entry whose parent is to be obtained.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookupp(cache_entry_t * pentry,
                                   hash_table_t * ht,
                                   cache_inode_client_t * pclient,
                                   fsal_op_context_t * pcontext,
                                   cache_inode_status_t * pstatus)
{
  return cache_inode_lookupp_sw(pentry, ht, pclient, pcontext, pstatus, TRUE);
}                               /* cache_inode_lookupp_sw */

/**
 *
 * cache_inode_lookupp_no_mutex: looks up (and caches) the parent directory for a directory. No mutex management
 * 
 * Looks up (and caches) the parent directory for a directory.
 *
 * @param pentry [IN] entry whose parent is to be obtained.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookupp_no_mutex(cache_entry_t * pentry,
                                            hash_table_t * ht,
                                            cache_inode_client_t * pclient,
                                            fsal_op_context_t * pcontext,
                                            cache_inode_status_t * pstatus)
{
  return cache_inode_lookupp_sw(pentry, ht, pclient, pcontext, pstatus, FALSE);
}                               /* cache_inode_lookupp_no_mutex */
