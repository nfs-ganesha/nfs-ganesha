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
 * \file    cache_inode_setattr.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/14 11:47:40 $
 * \version $Revision: 1.19 $
 * \brief   Sets the attributes for an entry.
 *
 * cache_inode_setattr.c : Sets the attributes for an entry.
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
 * cache_inode_async_setattr: set the attributes (to be called from a synclet)
 *
 * Sets the attributes for an entry located in the cache by its address. Attributes are provided
 * with compliance to the underlying FSAL semantics. Function is to be called from a synclet
 *
 * @param popasyncdesc [IN] Cache Inode Asynchonous Operation descriptor
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */

fsal_status_t cache_inode_async_setattr(cache_inode_async_op_desc_t * popasyncdesc)
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_setattrs(popasyncdesc->op_args.setattr.pfsal_handle,
                              &popasyncdesc->fsal_op_context,
                              &popasyncdesc->op_args.setattr.attr,
                              &popasyncdesc->op_res.setattr.attr);

  return fsal_status;
}                               /* cache_inode_aync_setattr */

/**
 *
 * cache_inode_setattr: set the attributes for an entry located in the cache by its address. 
 * 
 * Sets the attributes for an entry located in the cache by its address. Attributes are provided 
 * with compliance to the underlying FSAL semantics. Attributes that are set are returned in "*pattr".
 *
 * @param pentry_parent [IN] entry for the parent directory to be managed.
 * @param pattr [INOUT] attributes for the entry that we have found. Out: attributes set.
 * @param ht [INOUT] hash table used for the cache.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if alloc/objectation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_setattr(cache_entry_t * pentry,
                                         fsal_attrib_list_t * pattr,
                                         hash_table_t * ht,
                                         cache_inode_client_t * pclient,
                                         fsal_op_context_t * pcontext,
                                         cache_inode_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle;
  fsal_status_t fsal_status;
  fsal_attrib_list_t *p_object_attributes;
  fsal_attrib_list_t candidate_attributes;
  int do_trunc = 0;
  cache_inode_async_op_desc_t *pasyncopdesc = NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stat */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_SETATTR] += 1;

  /* Lock the entry */
  P(pentry->lock);

  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      pfsal_handle = &pentry->object.file.handle;
      p_object_attributes = &(pentry->object.file.attributes);
      break;

    case SYMBOLIC_LINK:
      pfsal_handle = &pentry->object.symlink.handle;
      p_object_attributes = &(pentry->object.symlink.attributes);
      break;

    case DIR_BEGINNING:
      pfsal_handle = &pentry->object.dir_begin.handle;
      p_object_attributes = &(pentry->object.dir_begin.attributes);
      break;

    case DIR_CONTINUE:
      /* lock the related dir_begin (dir begin are garbagge collected AFTER their related dir_cont)
       * this means that if a DIR_CONTINUE exists, its pdir pointer is not endless */
      P(pentry->object.dir_cont.pdir_begin->lock);
      pfsal_handle = &pentry->object.dir_cont.pdir_begin->object.dir_begin.handle;
      p_object_attributes =
          &pentry->object.dir_cont.pdir_begin->object.dir_begin.attributes;
      V(pentry->object.dir_cont.pdir_begin->lock);
      break;

    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    case FIFO_FILE:
      pfsal_handle = &pentry->object.special_obj.handle;
      p_object_attributes = &(pentry->object.special_obj.attributes);
      break;
    }

  /* Build candidate attributes */
  fsal_status = FSAL_merge_attrs(p_object_attributes, pattr, &candidate_attributes);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      V(pentry->lock);

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_SETATTR] += 1;

      return *pstatus;
    }

  /* Check witinh the attributes is we can do this setattr or not */
  fsal_status = FSAL_setattr_access(pcontext, &candidate_attributes, p_object_attributes);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      V(pentry->lock);

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_SETATTR] += 1;

      return *pstatus;
    }

  /* Post an asynchronous operation */
  P(pclient->pool_lock);
  GET_PREALLOC(pasyncopdesc,
               pclient->pool_async_op,
               pclient->nb_pre_async_op_desc, cache_inode_async_op_desc_t, next_alloc);
  V(pclient->pool_lock);

  if(pasyncopdesc == NULL)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_SETATTR] += 1;

      *pstatus = CACHE_INODE_MALLOC_ERROR;

      V(pentry->lock);

      return *pstatus;
    }

  pasyncopdesc->op_type = CACHE_INODE_ASYNC_OP_SETATTR;
  pasyncopdesc->op_args.setattr.pfsal_handle = &(pentry->object.file.handle);
  pasyncopdesc->op_args.setattr.attr = *pattr;
  pasyncopdesc->op_res.setattr.attr.asked_attributes = FSAL_ATTRS_POSIX;
  pasyncopdesc->op_func = cache_inode_async_setattr;

  pasyncopdesc->fsal_op_context = *pcontext;
  pasyncopdesc->fsal_export_context = *(pcontext->export_context);
  pasyncopdesc->fsal_op_context.export_context = &pasyncopdesc->fsal_export_context;

  pasyncopdesc->ht = ht;
  pasyncopdesc->origine_pool = pclient->pool_async_op;
  pasyncopdesc->ppool_lock = &pclient->pool_lock;

  if(gettimeofday(&pasyncopdesc->op_time, NULL) != 0)
    {
      /* Could'not get time of day... Stopping, this may need a major failure */
      DisplayLog("cache_inode_setattr: cannot get time of day... exiting");
      exit(1);
    }

  /* Affect the operation to a synclet */
  if(cache_inode_post_async_op(pasyncopdesc, pentry, pstatus) != CACHE_INODE_SUCCESS)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_SETATTR] += 1;

      DisplayLog("WARNING !!! cache_inode_setattr could not post async op....");

      *pstatus = CACHE_INODE_ASYNC_POST_ERROR;
      V(pentry->lock);

      return *pstatus;
    }

  /* Update the cached attributes */
  if((candidate_attributes.asked_attributes & FSAL_ATTR_SIZE) ||
     (candidate_attributes.asked_attributes & FSAL_ATTR_SPACEUSED))
    {

      if(pentry->internal_md.type == REGULAR_FILE)
        {
          if(pentry->object.file.pentry_content == NULL)
            {
              /* Operation on a non data cached file */
              p_object_attributes->filesize = candidate_attributes.filesize;
              p_object_attributes->spaceused = candidate_attributes.filesize;   /* Unclear hook here. BUGAZOMEU */
            }
          else
            {
              /* Data cached file */
              /* Do not set the p_object_attributes->filesize and p_object_attributes->spaceused  in this case 
               * This will lead to a situation where (for example) untar-ing a file will produced invalid files 
               * with a size of 0 despite the fact that they are not empty */
#ifdef  _DEBUG_CACHE_INODE
              DisplayLogJdLevel(pclient->log_outputs, NIV_FULL_DEBUG,
                                "cache_inode_setattr with FSAL_ATTR_SIZE on data cached entry");
#endif
            }
        }
      else if(pattr->asked_attributes & FSAL_ATTR_SIZE)
        DisplayLog
            ("WARNING !!! cache_inode_setattr tryed to operate size on a non REGULAR_FILE type=%d",
             pentry->internal_md.type);
    }

  if(candidate_attributes.asked_attributes &
     (FSAL_ATTR_MODE | FSAL_ATTR_OWNER | FSAL_ATTR_GROUP))
    {
      if(candidate_attributes.asked_attributes & FSAL_ATTR_MODE)
        p_object_attributes->mode = candidate_attributes.mode;

      if(candidate_attributes.asked_attributes & FSAL_ATTR_OWNER)
        p_object_attributes->owner = candidate_attributes.owner;

      if(candidate_attributes.asked_attributes & FSAL_ATTR_GROUP)
        p_object_attributes->group = candidate_attributes.group;
    }

  if(candidate_attributes.asked_attributes &
     (FSAL_ATTR_ATIME | FSAL_ATTR_CTIME | FSAL_ATTR_MTIME))
    {
      if(candidate_attributes.asked_attributes & FSAL_ATTR_ATIME)
        p_object_attributes->atime = candidate_attributes.atime;

      if(candidate_attributes.asked_attributes & FSAL_ATTR_CTIME)
        p_object_attributes->ctime = candidate_attributes.ctime;

      if(candidate_attributes.asked_attributes & FSAL_ATTR_MTIME)
        p_object_attributes->mtime = candidate_attributes.mtime;
    }

  /* Return the attributes as set */
  *pattr = *p_object_attributes;

  /* validate the entry */
  *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_SET, pclient);

  /* Release the entry */
  V(pentry->lock);

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_SETATTR] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_SETATTR] += 1;

  return *pstatus;
}                               /* cache_inode_setattr */
