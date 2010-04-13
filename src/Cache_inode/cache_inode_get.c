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
 * \file    cache_inode_get.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.26 $
 * \brief   Get and eventually cache an entry.
 *
 * cache_inode_get.c : Get and eventually cache an entry.
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
 * cache_inode_get: Gets an entry by using its fsdata as a key and caches it if needed.
 * 
 * Gets an entry by using its fsdata as a key and caches it if needed.
 * ASSUMPTION: DIR_CONT entries are always garbabbaged before their related DIR_BEGINNG 
 *
 * @param fsdata [IN] file system data
 * @param pattr [OUT] pointer to the attributes for the result. 
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return the pointer to the entry is successfull, NULL otherwise.
 *
 */
cache_entry_t *cache_inode_get(cache_inode_fsal_data_t * pfsdata,
                               fsal_attrib_list_t * pattr,
                               hash_table_t * ht,
                               cache_inode_client_t * pclient,
                               fsal_op_context_t * pcontext,
                               cache_inode_status_t * pstatus)
{
  hash_buffer_t key, value;
  cache_entry_t *pentry = NULL;
  fsal_status_t fsal_status;
  cache_inode_create_arg_t create_arg;
  cache_inode_file_type_t type;
  int hrc = 0;
  fsal_attrib_list_t fsal_attributes;
  cache_inode_fsal_data_t *ppoolfsdata = NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_GET] += 1;

  /* Turn the input to a hash key */
  if(cache_inode_fsaldata_2_key(&key, pfsdata, pclient))
    {
      *pstatus = CACHE_INODE_UNAPPROPRIATED_KEY;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_GET] += 1;

      ppoolfsdata = (cache_inode_fsal_data_t *) key.pdata;
      RELEASE_PREALLOC(ppoolfsdata, pclient->pool_key, next_alloc);

      return NULL;
    }

  switch (hrc = HashTable_Get(ht, &key, &value))
    {
    case HASHTABLE_SUCCESS:
      /* Entry exists in the cache and was found */
      pentry = (cache_entry_t *) value.pdata;

      /* return attributes additionally */
      cache_inode_get_attributes(pentry, pattr);

      break;

    case HASHTABLE_ERROR_NO_SUCH_KEY:
      /* Cache miss, allocate a new entry */

      /* If we ask for a dir cont (in this case pfsdata.cookie != FSAL_DIR_BEGINNING, we have 
       * a client who performs a readdir in the middle of a directory, when the direcctories
       * have been garbbage. we must search for the DIR_BEGIN related to this DIR_CONT */
      if(pfsdata->cookie != DIR_START)
        {
          /* added for sanity check */
          DisplayLog
              ("=======> Pb cache_inode_get: line %u pfsdata->cookie != DIR_START (=%u) on object whose type is %u",
               __LINE__, pfsdata->cookie,
               cache_inode_fsal_type_convert(fsal_attributes.type));

          pfsdata->cookie = DIR_START;

          /* Free this key */
          cache_inode_release_fsaldata_key(&key, pclient);

          /* redo the call */
          return cache_inode_get(pfsdata, pattr, ht, pclient, pcontext, pstatus);
        }

      /* First, call FSAL to know what the object is */
      fsal_attributes.asked_attributes = pclient->attrmask;
      fsal_status = FSAL_getattrs(&pfsdata->handle, pcontext, &fsal_attributes);
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          DisplayLog("cache_inode_get: line %u cache_inode_status=%u fsal_status=%u,%u ",
                     __LINE__, *pstatus, fsal_status.major, fsal_status.minor);

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              char handle_str[256];

              snprintHandle(handle_str, 256, &pfsdata->handle);
              DisplayLog("cache_inode_get: Stale FSAL File Handle %s", handle_str);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_GET] += 1;

          /* Free this key */
          cache_inode_release_fsaldata_key(&key, pclient);

          return NULL;
        }

      /* The type has to be set in the attributes */
      if(!FSAL_TEST_MASK(fsal_attributes.supported_attributes, FSAL_ATTR_TYPE))
        {
          *pstatus = CACHE_INODE_FSAL_ERROR;

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_GET] += 1;

          /* Free this key */
          cache_inode_release_fsaldata_key(&key, pclient);

          return NULL;
        }

      /* Get the cache_inode file type */
      type = cache_inode_fsal_type_convert(fsal_attributes.type);

      if(type == SYMBOLIC_LINK)
        {
          FSAL_CLEAR_MASK(fsal_attributes.asked_attributes);
          FSAL_SET_MASK(fsal_attributes.asked_attributes, pclient->attrmask);
          fsal_status =
              FSAL_readlink(&pfsdata->handle, pcontext, &create_arg.link_content,
                            &fsal_attributes);
          if(FSAL_IS_ERROR(fsal_status))
            {
              *pstatus = cache_inode_error_convert(fsal_status);

              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_GET] += 1;

              /* Free this key */
              cache_inode_release_fsaldata_key(&key, pclient);

              if(fsal_status.major == ERR_FSAL_STALE)
                {
                  cache_inode_status_t kill_status;

                  DisplayLog
                      ("cache_inode_get: Stale FSAL File Handle detected for pentry = %p",
                       pentry);

                  if(cache_inode_kill_entry(pentry, ht, pclient, &kill_status) !=
                     CACHE_INODE_SUCCESS)
                    DisplayLog("cache_inode_get: Could not kill entry %p, status = %u",
                               pentry, kill_status);

                  *pstatus = CACHE_INODE_FSAL_ESTALE;

                }

              return NULL;
            }
        }

      /* Add the entry to the cache */
      if((pentry = cache_inode_new_entry(pfsdata, &fsal_attributes, type, &create_arg, NULL,    /* never used to add a new DIR_CONTINUE within the scope of this function */
                                         ht, pclient, pcontext, FALSE,  /* This is a population, not a creation */
                                         pstatus)) == NULL)
        {
          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_GET] += 1;

          /* Free this key */
          cache_inode_release_fsaldata_key(&key, pclient);

          return NULL;
        }

      /* Set the returned attributes */
      *pattr = fsal_attributes;

      /* Now, exit the switch/case and returns */
      break;

    default:
      /* This should not happened */
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_GET] += 1;

      /* Free this key */
      cache_inode_release_fsaldata_key(&key, pclient);

      return NULL;
      break;
    }

  *pstatus = CACHE_INODE_SUCCESS;

  /* valid the found entry, if this is not feasable, returns nothing to the client */
  P_w(&pentry->lock);
  if((*pstatus =
      cache_inode_valid(pentry, CACHE_INODE_OP_GET, pclient)) != CACHE_INODE_SUCCESS)
    {
      V_w(&pentry->lock);
      pentry = NULL;
    }
  V_w(&pentry->lock);

  /* stats */
  pclient->stat.func_stats.nb_success[CACHE_INODE_GET] += 1;

  /* Free this key */
  cache_inode_release_fsaldata_key(&key, pclient);

  return pentry;
}                               /* cache_inode_get */
