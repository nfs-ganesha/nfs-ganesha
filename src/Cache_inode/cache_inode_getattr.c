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
 * \file    cache_inode_getattr.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.17 $
 * \brief   Gets the attributes for an entry.
 *
 * cache_inode_getattr.c : Gets the attributes for an entry.
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
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_getattr: Gets the attributes for a cached entry.
 * 
 * Gets the attributes for a cached entry. The FSAL attributes are kept in a structure when the entry
 * is added to the cache.
 *
 * @param pentry [IN] entry to be managed.
 * @param pattr [OUT] pointer to the results 
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_getattr(cache_entry_t * pentry, fsal_attrib_list_t * pattr, hash_table_t * ht,	/* Unused, kept for protototype's homogeneity */
					 cache_inode_client_t * pclient,
					 fsal_op_context_t * pcontext,
					 cache_inode_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle;
  fsal_status_t fsal_status;

  /* sanity check */
  if (pentry == NULL || pattr == NULL || ht == NULL || pclient == NULL
      || pcontext == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_GETATTR] += 1;

  /* Lock the entry */
  P_w(&pentry->lock);
  if (cache_inode_renew_entry(pentry, pattr, ht, pclient, pcontext, pstatus) !=
      CACHE_INODE_SUCCESS)
    {
      V_w(&pentry->lock);
      pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_GETATTR] += 1;
      return *pstatus;
    }

  /* RW Lock goes for writer to reader */
  rw_lock_downgrade(&pentry->lock);

  cache_inode_get_attributes(pentry, pattr);

  if (FSAL_TEST_MASK(pattr->asked_attributes, FSAL_ATTR_RDATTR_ERR))
    {
      switch (pentry->internal_md.type)
	{
	case REGULAR_FILE:
	  pfsal_handle = &pentry->object.file.handle;
	  break;

	case SYMBOLIC_LINK:
	  pfsal_handle = &pentry->object.symlink.handle;
	  break;

	case DIR_BEGINNING:
	  pfsal_handle = &pentry->object.dir_begin.handle;
	  break;

	case DIR_CONTINUE:
	  /* lock the related dir_begin (dir begin are garbagge collected AFTER their related dir_cont)
	   * this means that if a DIR_CONTINUE exists, its pdir pointer is not endless */
	  P_r(&pentry->object.dir_cont.pdir_begin->lock);
	  pfsal_handle = &pentry->object.dir_cont.pdir_begin->object.dir_begin.handle;
	  V_r(&pentry->object.dir_cont.pdir_begin->lock);
	  break;

	case SOCKET_FILE:
	case FIFO_FILE:
	case BLOCK_FILE:
	case CHARACTER_FILE:
	  pfsal_handle = &pentry->object.special_obj.handle;
	  break;

	}

      /* An error occured when trying to get the attributes, they have to be renewed */
      fsal_status = FSAL_getattrs(pfsal_handle, pcontext, pattr);
      if (FSAL_IS_ERROR(fsal_status))
	{
	  *pstatus = cache_inode_error_convert(fsal_status);
	  V_r(&pentry->lock);

	  if (fsal_status.major == ERR_FSAL_STALE)
	    {
	      cache_inode_status_t kill_status;

	      DisplayLog
		  ("cache_inode_getattr: Stale FSAL File Handle detected for pentry = %p",
		   pentry);

	      if (cache_inode_kill_entry(pentry, ht, pclient, &kill_status) !=
		  CACHE_INODE_SUCCESS)
		DisplayLog("cache_inode_getattr: Could not kill entry %p, status = %u",
			   pentry, kill_status);

	      *pstatus = CACHE_INODE_FSAL_ESTALE;
	    }

	  /* stat */
	  pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_GETATTR] += 1;

	  return *pstatus;
	}

      /* Set the new attributes */
      cache_inode_set_attributes(pentry, pattr);
    }

  *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_GET, pclient);

  V_r(&pentry->lock);

  /* stat */
  if (*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_GETATTR] += 1;
    else
    pclient->stat.func_stats.nb_success[CACHE_INODE_GETATTR] += 1;

  return *pstatus;
}				/* cache_inode_getattr */
