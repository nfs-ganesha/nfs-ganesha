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
 * \file    cache_inode_lookup.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.33 $
 * \brief   Perform lookup through the cache.
 *
 * cache_inode_lookup.c : Perform lookup through the cache.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
 * cache_inode_lookup_sw: looks up for a name in a directory indicated by a cached entry.
 * 
 * Looks up for a name in a directory indicated by a cached entry. The directory should have been cached before.
 *
 * @param pentry_parent [IN]    entry for the parent directory to be managed.
 * @param name          [IN]    name of the entry that we are looking for in the cache.
 * @param pattr         [OUT]   attributes for the entry that we have found.
 * @param ht            [IN]    hash table used for the cache, unused in this call.
 * @param pclient       [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext         [IN]    FSAL credentials 
 * @param pstatus       [OUT]   returned status.
 * @param use_mutex     [IN]    if TRUE, mutex management is done, not if equal to FALSE.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookup_sw(cache_entry_t * pentry_parent,
                                     fsal_name_t * pname,
                                     fsal_attrib_list_t * pattr,
                                     hash_table_t * ht,
                                     cache_inode_client_t * pclient,
                                     fsal_op_context_t * pcontext,
                                     cache_inode_status_t * pstatus, int use_mutex)
{
  cache_entry_t *pdir_chain = NULL;
  cache_entry_t *pentry = NULL;
  fsal_status_t fsal_status;
  fsal_handle_t object_handle;
  fsal_handle_t dir_handle;
  fsal_attrib_list_t object_attributes;
  cache_inode_status_t status;
  cache_inode_create_arg_t create_arg;
  cache_inode_file_type_t type;
  cache_inode_status_t cache_status;

  cache_inode_fsal_data_t new_entry_fsdata;
  int i = 0;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_LOOKUP] += 1;

  /* Entry should not be dead */
  if (pentry_parent->async_health != CACHE_INODE_ASYNC_STAYING_ALIVE)
    {
      *pstatus = CACHE_INODE_DEAD_ENTRY;
      return NULL;
    }

  /* Get lock on the pentry */
  if (use_mutex == TRUE)
    P(pentry_parent->lock);

  if (pentry_parent->internal_md.type != DIR_BEGINNING &&
      pentry_parent->internal_md.type != DIR_CONTINUE)
    {
      /* Parent is no directory base, return NULL */
      *pstatus = CACHE_INODE_NOT_A_DIRECTORY;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP] += 1;

      if (use_mutex == TRUE)
        V(pentry_parent->lock);

      return NULL;
    }

  /* if name is ".", use the input value */
  if (!FSAL_namecmp(pname, &FSAL_DOT))
    {
      pentry = pentry_parent;
  } else if (!FSAL_namecmp(pname, &FSAL_DOT_DOT))
    {
      /* Directory do only have exactly one parent. This a limitation in all FS, which 
       * implies that hard link are forbidden on directories (so that they exists only in one dir)
       * Because of this, the parent list is always limited to one element for a dir.
       * Clients SHOULD never 'lookup( .. )' in something that is no dir */
      pentry =
          cache_inode_lookupp_no_mutex(pentry_parent, ht, pclient, pcontext, pstatus);
    } else
    {
      /* This is a "regular lookup" (not on "." or "..") */

      /* Check is user (as specified by the credentials) is authorized to lookup the directory or not */
      if (cache_inode_access_no_mutex(pentry_parent,
                                      FSAL_X_OK,
                                      ht,
                                      pclient, pcontext, pstatus) != CACHE_INODE_SUCCESS)
        {
          if (use_mutex == TRUE)
            V(pentry_parent->lock);

          pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_GETATTR] += 1;
          return NULL;
        }

      /* Try to look into the dir and its dir_cont. At this point, it must be said than lock on dir_cont are
       *  taken when a lock is previously acquired on the related dir_begin */
      pdir_chain = pentry_parent;

      do
        {
          /* Is this entry known ? */
          if (pdir_chain->internal_md.type == DIR_BEGINNING)
            {
              for (i = 0; i < CHILDREN_ARRAY_SIZE; i++)
                {
                  if (pdir_chain->object.dir_begin.pdir_data->dir_entries[i].active ==
                      VALID)
                    if (!FSAL_namecmp
                        (pname,
                         &(pentry_parent->object.dir_begin.pdir_data->dir_entries[i].
                           name)))
                      {
                        /* Entry was found */
                        pentry =
                            pentry_parent->object.dir_begin.pdir_data->dir_entries[i].
                            pentry;
                        DisplayLogJdLevel(pclient->log_outputs, NIV_FULL_DEBUG,
                                          "Cache Hit detected (dir_begin)");
                        break;
                      }
                }

              /* Do we have to go on browsing the cache_inode ? */
              if (pdir_chain->object.dir_begin.end_of_dir == END_OF_DIR)
                {
                  break;
                }

              /* Next step */
              pdir_chain = pdir_chain->object.dir_begin.pdir_cont;
            } else
            {
              /* The element in the dir_chain is a DIR_CONTINUE */
              for (i = 0; i < CHILDREN_ARRAY_SIZE; i++)
                {
                  if (pdir_chain->object.dir_cont.pdir_data->dir_entries[i].active ==
                      VALID)
                    if (!FSAL_namecmp
                        (pname,
                         &(pdir_chain->object.dir_cont.pdir_data->dir_entries[i].name)))
                      {
                        /* Entry was found */
                        pentry =
                            pdir_chain->object.dir_cont.pdir_data->dir_entries[i].pentry;
                        DisplayLogJdLevel(pclient->log_outputs, NIV_FULL_DEBUG,
                                          "Cache Hit detected (dir_cont)");
                        break;
                      }
                }

              /* Do we have to go on browsing the cache_inode ? */
              if (pdir_chain->object.dir_cont.end_of_dir == END_OF_DIR)
                {
                  break;
                }

              /* Next step */
              pdir_chain = pdir_chain->object.dir_cont.pdir_cont;
            }

        }
      while (pentry == NULL);

      /* At this point, if pentry == NULL, we are not looking for a known son, query fsal for lookup */
      if (pentry == NULL)
        {
          DisplayLogJdLevel(pclient->log_outputs, NIV_FULL_DEBUG, "Cache Miss detected");

          if (pentry_parent->internal_md.type == DIR_BEGINNING)
            dir_handle = pentry_parent->object.dir_begin.handle;

          if (pentry_parent->internal_md.type == DIR_CONTINUE)
            {
              if (use_mutex == TRUE)
                P(pentry_parent->object.dir_cont.pdir_begin->lock);

              dir_handle =
                  pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.handle;

              if (use_mutex == TRUE)
                V(pentry_parent->object.dir_cont.pdir_begin->lock);
            }

          object_attributes.asked_attributes = pclient->attrmask;
          fsal_status =
              FSAL_lookup(&dir_handle, pname, pcontext, &object_handle,
                          &object_attributes);

          if (FSAL_IS_ERROR(fsal_status))
            {
              *pstatus = cache_inode_error_convert(fsal_status);

              if (use_mutex == TRUE)
                V(pentry_parent->lock);

              /* Stale File Handle to be detected and managed */
              if (fsal_status.major == ERR_FSAL_STALE)
                {
                  cache_inode_status_t kill_status;

                  DisplayLog
                      ("cache_inode_lookup: Stale FSAL File Handle detected for pentry = %p",
                       pentry_parent);

                  if (cache_inode_kill_entry(pentry_parent, ht, pclient, &kill_status) !=
                      CACHE_INODE_SUCCESS)
                    DisplayLog
                        ("cache_inode_pentry_parent: Could not kill entry %p, status = %u",
                         pentry_parent, kill_status);

                  *pstatus = CACHE_INODE_FSAL_ESTALE;
                }

              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP] += 1;

              return NULL;
            }

          type = cache_inode_fsal_type_convert(object_attributes.type);

          /* If entry is a symlink, this value for be cached */
          if (type == SYMBOLIC_LINK)
            {
              fsal_status =
                  FSAL_readlink(&object_handle, pcontext, &create_arg.link_content,
                                &object_attributes);
              if (FSAL_IS_ERROR(fsal_status))
                {
                  *pstatus = cache_inode_error_convert(fsal_status);
                  if (use_mutex == TRUE)
                    V(pentry_parent->lock);

                  /* Stale File Handle to be detected and managed */
                  if (fsal_status.major == ERR_FSAL_STALE)
                    {
                      cache_inode_status_t kill_status;

                      DisplayLog
                          ("cache_inode_lookup: Stale FSAL File Handle detected for pentry = %p",
                           pentry_parent);

                      if (cache_inode_kill_entry(pentry_parent, ht, pclient, &kill_status)
                          != CACHE_INODE_SUCCESS)
                        DisplayLog
                            ("cache_inode_pentry_parent: Could not kill entry %p, status = %u",
                             pentry_parent, kill_status);

                      *pstatus = CACHE_INODE_FSAL_ESTALE;
                    }

                  /* stats */
                  pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP] += 1;

                  return NULL;
                }
            }

          /* Allocation of a new entry in the cache */
          new_entry_fsdata.handle = object_handle;
          new_entry_fsdata.cookie = 0;

          if ((pentry = cache_inode_new_entry(&new_entry_fsdata, &object_attributes, type, &create_arg, NULL, ht, pclient, pcontext, FALSE,     /* This is a population and not a creation */
                                              pstatus)) == NULL)
            {
              if (use_mutex == TRUE)
                V(pentry_parent->lock);

              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP] += 1;

              return NULL;
            }

          /* Link entry to the parent */
          pentry->parent_list->parent = pentry_parent;

          /* Entry was found in the FSAL, add this entry to the parent directory */
          cache_status = cache_inode_add_cached_dirent(pentry_parent,
                                                       pname,
                                                       pentry,
                                                       NULL,
                                                       ht, pclient, pcontext, pstatus);

          if (cache_status != CACHE_INODE_SUCCESS
              && cache_status != CACHE_INODE_ENTRY_EXISTS)
            {
              if (use_mutex == TRUE)
                V(pentry_parent->lock);

              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP] += 1;

              return NULL;
            }

        }
    }

  /* if the found pentry is dead, then file has been deleted, returns ENOENT */
  if (pentry->async_health != CACHE_INODE_ASYNC_STAYING_ALIVE)
    {
      *pstatus = CACHE_INODE_NOT_FOUND;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP] += 1;

      if (use_mutex == TRUE)
        V(pentry_parent->lock);

      return NULL;
    }
  /* Return the attributes */
  cache_inode_get_attributes(pentry, pattr);

  *pstatus = cache_inode_valid(pentry_parent, CACHE_INODE_OP_GET, pclient);
  if (use_mutex == TRUE)
    V(pentry_parent->lock);

  /* stat */
  if (*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_LOOKUP] += 1;
    else
    pclient->stat.func_stats.nb_success[CACHE_INODE_LOOKUP] += 1;

  return pentry;
}                               /* cache_inode_lookup_sw */

/**
 *
 * cache_inode_lookup_no_mutex: looks up for a name in a directory indicated by a cached entry (no mutex management).
 * 
 * Looks up for a name in a directory indicated by a cached entry. The directory should have been cached before.
 * This function has no mutex management and suppose that is it properly done in the clling function
 *
 * @param pentry_parent [IN]    entry for the parent directory to be managed.
 * @param name          [IN]    name of the entry that we are looking for in the cache.
 * @param pattr         [OUT]   attributes for the entry that we have found.
 * @param ht            [IN]    hash table used for the cache, unused in this call.
 * @param pclient       [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext         [IN]    FSAL credentials 
 * @param pstatus       [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookup_no_mutex(cache_entry_t * pentry_parent,
                                           fsal_name_t * pname,
                                           fsal_attrib_list_t * pattr,
                                           hash_table_t * ht,
                                           cache_inode_client_t * pclient,
                                           fsal_op_context_t * pcontext,
                                           cache_inode_status_t * pstatus)
{
  return cache_inode_lookup_sw(pentry_parent,
                               pname, pattr, ht, pclient, pcontext, pstatus, FALSE);
}                               /* cache_inode_lookup_no_mutex */

/**
 *
 * cache_inode_lookup: looks up for a name in a directory indicated by a cached entry.
 * 
 * Looks up for a name in a directory indicated by a cached entry. The directory should have been cached before.
 *
 * @param pentry_parent [IN]    entry for the parent directory to be managed.
 * @param name          [IN]    name of the entry that we are looking for in the cache.
 * @param pattr         [OUT]   attributes for the entry that we have found.
 * @param ht            [IN]    hash table used for the cache, unused in this call.
 * @param pclient       [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext         [IN]    FSAL credentials 
 * @param pstatus       [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookup(cache_entry_t * pentry_parent,
                                  fsal_name_t * pname,
                                  fsal_attrib_list_t * pattr,
                                  hash_table_t * ht,
                                  cache_inode_client_t * pclient,
                                  fsal_op_context_t * pcontext,
                                  cache_inode_status_t * pstatus)
{
  return cache_inode_lookup_sw(pentry_parent,
                               pname, pattr, ht, pclient, pcontext, pstatus, TRUE);
}                               /* cache_inode_lookup */
