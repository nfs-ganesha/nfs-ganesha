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
 * \file    cache_inode_access.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.19 $
 * \brief   Check for object accessibility.
 *
 * cache_inode_access.c : Check for object accessibility.
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
 * cache_inode_access: checks for an entry accessibility.
 * 
 * Checks for an entry accessibility.
 *
 * @param pentry      [IN]    entry pointer for the fs object to be checked. 
 * @param access_type [IN]    flags used to describe the kind of access to be checked. 
 * @param ht          [INOUT] hash table used for the cache.
 * @param pclient     [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext    [IN]    FSAL context 
 * @param pstatus     [OUT]   returned status.
 * @param use_mutex   [IN]    a flag to tell if mutex are to be used or not. 
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry \n
 * @return any other values show an unauthorized access.
 *
 */
cache_inode_status_t cache_inode_access_sw(cache_entry_t * pentry,
                                           fsal_accessflags_t access_type,
                                           hash_table_t * ht,
                                           cache_inode_client_t * pclient,
                                           fsal_op_context_t * pcontext,
                                           cache_inode_status_t * pstatus, int use_mutex)
{
  fsal_attrib_list_t attr;
  fsal_status_t fsal_status;
  cache_inode_status_t cache_status;
  fsal_accessflags_t used_access_type;
  fsal_handle_t *pfsal_handle = NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_ACCESS] += 1;

  if (use_mutex)
    P_r(&pentry->lock);

  /* We do no explicit access test in FSAL for FSAL_F_OK: it is considered that if 
   * an entry resides in the cache_inode, then a FSAL_getattrs was successfully made
   * to populate the cache entry, this means that the entry exists. For this reason, 
   * F_OK is managed internally */
  if (access_type != FSAL_F_OK)
    {
      /* We get ride of F_OK */
      used_access_type = access_type & ~FSAL_F_OK;

      /* We get the attributes */
      cache_inode_get_attributes(pentry, &attr);

      /* Function FSAL_test_access is used instead of FSAL_access. This allow
       * to take benefit of the previously cached attributes. This behavior
       * is configurable via the configuration file. */

      if (pclient->use_test_access == 1)
        {
          /* We get the attributes */
          cache_inode_get_attributes(pentry, &attr);

          fsal_status = FSAL_test_access(pcontext, used_access_type, &attr);
        }
      else
        {
          if ((pfsal_handle = cache_inode_get_fsal_handle(pentry, pstatus)) == NULL)
            {
              if (use_mutex)
                V_r(&pentry->lock);
              return *pstatus;
            }
#ifdef _USE_MFSL
          fsal_status =
              MFSL_access(&pentry->mobject, pcontext, &pclient->mfsl_context,
                          used_access_type, &attr);
#else
          fsal_status = FSAL_access(pfsal_handle, pcontext, used_access_type, &attr);
#endif
        }

      if (FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_ACCESS] += 1;

          if (fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              DisplayLog
                  ("cache_inode_access: Stale FSAL File Handle detected for pentry = %p",
                   pentry);

              if (cache_inode_kill_entry(pentry, ht, pclient, &kill_status) !=
                  CACHE_INODE_SUCCESS)
                DisplayLog("cache_inode_access: Could not kill entry %p, status = %u",
                           pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;

              return *pstatus;
            }
        }
      else
        *pstatus = CACHE_INODE_SUCCESS;

    }

  if (*pstatus != CACHE_INODE_SUCCESS)
    {
      if (use_mutex)
        V_r(&pentry->lock);

      return *pstatus;
    }
  /* stats and validation */
  if ((cache_status =
       cache_inode_valid(pentry, CACHE_INODE_OP_GET, pclient)) != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_ACCESS] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_ACCESS] += 1;

  if (use_mutex)
    V_r(&pentry->lock);

  return *pstatus;
}                               /* cache_inode_access_sw */

/**
 *
 * cache_inode_access_no_mutex: checks for an entry accessibility. No mutex management
 * 
 * Checks for an entry accessibility.
 *
 * @param pentry      [IN]    entry pointer for the fs object to be checked. 
 * @param access_type [IN]    flags used to describe the kind of access to be checked. 
 * @param ht          [INOUT] hash table used for the cache.
 * @param pclient     [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext       [IN]    FSAL credentials 
 * @param pstatus     [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_access_no_mutex(cache_entry_t * pentry,
                                                 fsal_accessflags_t access_type,
                                                 hash_table_t * ht,
                                                 cache_inode_client_t * pclient,
                                                 fsal_op_context_t * pcontext,
                                                 cache_inode_status_t * pstatus)
{
  return cache_inode_access_sw(pentry,
                               access_type, ht, pclient, pcontext, pstatus, FALSE);
}                               /* cache_inode_access_no_mutex */

/**
 *
 * cache_inode_access: checks for an entry accessibility.
 * 
 * Checks for an entry accessibility.
 *
 * @param pentry      [IN]    entry pointer for the fs object to be checked. 
 * @param access_type [IN]    flags used to describe the kind of access to be checked. 
 * @param ht          [INOUT] hash table used for the cache.
 * @param pclient     [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext       [IN]    FSAL credentials 
 * @param pstatus     [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_access(cache_entry_t * pentry,
                                        fsal_accessflags_t access_type,
                                        hash_table_t * ht,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t * pcontext,
                                        cache_inode_status_t * pstatus)
{
  return cache_inode_access_sw(pentry, access_type, ht, pclient, pcontext, pstatus, TRUE);
}                               /* cache_inode_access */
