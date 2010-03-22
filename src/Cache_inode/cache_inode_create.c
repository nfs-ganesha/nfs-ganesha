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
 * \file    cache_inode_create.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.29 $
 * \brief   Creation of a file through the cache layer.
 *
 * cache_inode_mkdir.c : Creation of an entry through the cache layer
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
 * cache_inode_create: creates an entry through the cache.
 *
 * Creates an entry through the cache.
 *
 * @param pentry_parent [IN] pointer to the pentry parent
 * @param pname         [IN] pointer to the name of the object in the destination directory.
 * @param type          [IN] type of the object to be created.
 * @param mode          [IN] mode to be used at file creation
 * @param pcreate_arg   [IN] additional argument for object creation
 * @param pattr         [OUT] attributes for the new object.
 * @param ht            [INOUT] hash table used for the cache.
 * @param pclient       [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext      [IN] FSAL credentials
 * @param pstatus       [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry\n
 * @return CACHE_INODE_BAD_TYPE either source or destination have incorrect type\n
 * @return CACHE_INODE_ENTRY_EXISTS entry of that name already exists in destination.
 *
 */
cache_entry_t *cache_inode_create(cache_entry_t * pentry_parent,
                                  fsal_name_t * pname,
                                  cache_inode_file_type_t type,
                                  fsal_accessmode_t mode,
                                  cache_inode_create_arg_t * pcreate_arg,
                                  fsal_attrib_list_t * pattr,
                                  hash_table_t * ht,
                                  cache_inode_client_t * pclient,
                                  fsal_op_context_t * pcontext,
                                  cache_inode_status_t * pstatus)
{
  cache_entry_t *pentry = NULL;
  fsal_status_t fsal_status;
#ifdef _USE_MFSL
  mfsl_object_t object_handle;
#else
  fsal_handle_t object_handle;
#endif
  fsal_attrib_list_t parent_attributes;
  fsal_attrib_list_t object_attributes;
  fsal_handle_t dir_handle;
  cache_inode_fsal_data_t fsal_data;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_CREATE] += 1;

  /* Check if the required type is correct, with this function, we manage file, dir and symlink */
  if (type != REGULAR_FILE && type != DIR_BEGINNING && type != SYMBOLIC_LINK
      && type != SOCKET_FILE && type != FIFO_FILE && type != CHARACTER_FILE
      && type != BLOCK_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      return NULL;
    }

  /* Check if an entry of the same name exists */
  if ((pentry = cache_inode_lookup(pentry_parent,
                                   pname,
                                   &object_attributes,
                                   ht, pclient, pcontext, pstatus)) != NULL)
    {
      *pstatus = CACHE_INODE_ENTRY_EXISTS;

      if (pentry->internal_md.type != type)
        {
          /* Incompatible types, returns NULL */

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

          return NULL;
        }
      else
        {
          /* stats */
          pclient->stat.func_stats.nb_success[CACHE_INODE_CREATE] += 1;

          /* redondant creation, returned the previously created entry */
          return pentry;
        }
    }

  /* At this point, the entry was not found, this means that is doesn't exist is FSAL, we can create it */

  /* Get the lock for the parent */
  P_w(&pentry_parent->lock);

  if (pentry_parent->internal_md.type == DIR_BEGINNING)
    dir_handle = pentry_parent->object.dir_begin.handle;

  if (pentry_parent->internal_md.type == DIR_CONTINUE)
    {
      P_r(&pentry_parent->object.dir_cont.pdir_begin->lock);
      dir_handle = pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.handle;
      V_r(&pentry_parent->object.dir_cont.pdir_begin->lock);
    }

  object_attributes.asked_attributes = pclient->attrmask;
  switch (type)
    {
    case REGULAR_FILE:
#ifdef _USE_MFSL
      cache_inode_get_attributes(pentry_parent, &parent_attributes);
      fsal_status = MFSL_create(&pentry_parent->mobject,
                                pname,
                                pcontext,
                                &pclient->mfsl_context,
                                mode,
                                &object_handle, &object_attributes, &parent_attributes);
#else
      fsal_status = FSAL_create(&dir_handle,
                                pname,
                                pcontext, mode, &object_handle, &object_attributes);
#endif
      break;

    case DIR_BEGINNING:
#ifdef _USE_MFSL
      cache_inode_get_attributes(pentry_parent, &parent_attributes);
      fsal_status = MFSL_mkdir(&pentry_parent->mobject,
                               pname,
                               pcontext,
                               &pclient->mfsl_context,
                               mode,
                               &object_handle, &object_attributes, &parent_attributes);
#else
      fsal_status = FSAL_mkdir(&dir_handle,
                               pname, pcontext, mode, &object_handle, &object_attributes);
#endif
      break;

    case SYMBOLIC_LINK:
#ifdef _USE_MFSL
      cache_inode_get_attributes(pentry_parent, &object_attributes);
      fsal_status = MFSL_symlink(&pentry_parent->mobject,
                                 pname,
                                 &pcreate_arg->link_content,
                                 pcontext,
                                 &pclient->mfsl_context,
                                 mode, &object_handle, &object_attributes);
#else
      fsal_status = FSAL_symlink(&dir_handle,
                                 pname,
                                 &pcreate_arg->link_content,
                                 pcontext, mode, &object_handle, &object_attributes);
#endif
      break;

    case SOCKET_FILE:
#ifdef _USE_MFSL
      fsal_status = MFSL_mknode(&pentry_parent->mobject, pname, pcontext, &pclient->mfsl_context, mode, FSAL_TYPE_SOCK, NULL,   /* no dev_t needed for socket file */
                                &object_handle, &object_attributes);
#else
      fsal_status = FSAL_mknode(&dir_handle, pname, pcontext, mode, FSAL_TYPE_SOCK, NULL,       /* no dev_t needed for socket file */
                                &object_handle, &object_attributes);
#endif
      break;

    case FIFO_FILE:
#ifdef _USE_MFSL
      fsal_status = MFSL_mknode(&pentry_parent->mobject, pname, pcontext, &pclient->mfsl_context, mode, FSAL_TYPE_FIFO, NULL,   /* no dev_t needed for FIFO file */
                                &object_handle, &object_attributes);
#else
      fsal_status = FSAL_mknode(&dir_handle, pname, pcontext, mode, FSAL_TYPE_FIFO, NULL,       /* no dev_t needed for FIFO file */
                                &object_handle, &object_attributes);
#endif
      break;

    case BLOCK_FILE:
#ifdef _USE_MFSL
      fsal_status = MFSL_mknode(&pentry_parent->mobject,
                                pname,
                                pcontext,
                                &pclient->mfsl_context,
                                mode,
                                FSAL_TYPE_BLK,
                                &pcreate_arg->dev_spec,
                                &object_handle, &object_attributes);
#else
      fsal_status = FSAL_mknode(&dir_handle,
                                pname,
                                pcontext,
                                mode,
                                FSAL_TYPE_BLK,
                                &pcreate_arg->dev_spec,
                                &object_handle, &object_attributes);
#endif
      break;

    case CHARACTER_FILE:
#ifdef _USE_MFSL
      fsal_status = MFSL_mknode(&pentry_parent->mobject,
                                pname,
                                pcontext,
                                &pclient->mfsl_context,
                                mode,
                                FSAL_TYPE_CHR,
                                &pcreate_arg->dev_spec,
                                &object_handle, &object_attributes);
#else
      fsal_status = FSAL_mknode(&dir_handle,
                                pname,
                                pcontext,
                                mode,
                                FSAL_TYPE_CHR,
                                &pcreate_arg->dev_spec,
                                &object_handle, &object_attributes);
#endif
      break;

    default:
      /* we should never go there */
      *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;
      V_w(&pentry_parent->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      return NULL;
      break;
    }

  /* Check for the result */
  if (FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      V_w(&pentry_parent->lock);

      if (fsal_status.major == ERR_FSAL_STALE)
        {
          cache_inode_status_t kill_status;

          DisplayLog
              ("cache_inode_create: Stale FSAL File Handle detected for pentry = %p",
               pentry_parent);

          if (cache_inode_kill_entry(pentry_parent, ht, pclient, &kill_status) !=
              CACHE_INODE_SUCCESS)
            DisplayLog("cache_inode_create: Could not kill entry %p, status = %u",
                       pentry_parent, kill_status);

          *pstatus = CACHE_INODE_FSAL_ESTALE;
        }

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      return NULL;
    }
  else
    {
#ifdef _USE_MFSL
      fsal_data.handle = object_handle.handle;
#else
      fsal_data.handle = object_handle;
#endif
      fsal_data.cookie = DIR_START;

      /* This call will return NULL if failed */
      if ((pentry = cache_inode_new_entry(&fsal_data, &object_attributes, type, pcreate_arg, NULL, ht, pclient, pcontext, TRUE, /* This is a creation and not a population */
                                          pstatus)) == NULL)
        {
          *pstatus = CACHE_INODE_INSERT_ERROR;
          V_w(&pentry_parent->lock);

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

          return NULL;
        }
#ifdef _USE_MFSL
      /* Copy the MFSL object to the cache */
      memcpy((char *)&(pentry->mobject), (char *)&object_handle, sizeof(mfsl_object_t));
#endif

      /* Add this entry to the directory */
      if (cache_inode_add_cached_dirent(pentry_parent,
                                        pname,
                                        pentry,
                                        NULL,
                                        ht,
                                        pclient,
                                        pcontext, pstatus) != CACHE_INODE_SUCCESS)
        {
          V_w(&pentry_parent->lock);

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

          return NULL;
        }
    }

  /* Update the parent cached attributes */
  if (pentry_parent->internal_md.type == DIR_BEGINNING)
    {
      pentry_parent->object.dir_begin.attributes.mtime.seconds = time(NULL);
      pentry_parent->object.dir_begin.attributes.mtime.nseconds = 0;
      pentry_parent->object.dir_begin.attributes.ctime =
          pentry_parent->object.dir_begin.attributes.mtime;

      /* if the created object is a directory, it contains a link
       * to its parent : '..'. Thus the numlink attr must be increased.
       */
      if (type == DIR_BEGINNING)
        {
          pentry_parent->object.dir_begin.attributes.numlinks++;
        }

    }
  else
    {
      /* DIR_CONTINUE */
      pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.attributes.mtime.
          seconds = time(NULL);
      pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.attributes.mtime.
          seconds = 0;
      pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.attributes.ctime =
          pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.attributes.mtime;

      /* if the created object is a directory, it contains a link
       * to its parent : '..'. Thus the numlink attr must be increased.
       */
      if (type == DIR_BEGINNING)
        {
          pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.attributes.
              numlinks++;
        }

    }

  /* Get the attributes in return */
  *pattr = object_attributes;

  /* valid the parent */
  *pstatus = cache_inode_valid(pentry_parent, CACHE_INODE_OP_SET, pclient);

  /* release the lock for the parent */
  V_w(&pentry_parent->lock);

  /* stat */
  if (*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_CREATE] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_CREATE] += 1;

  return pentry;
}                               /* cache_inode_create */

/**
 *
 * cache_inode_create_open: creates a file and opens it at the same time (for NFSv4 semantics)
 *
 * Creates an entry through the cache.
 *
 * @param pentry_parent [IN] pointer to the pentry parent
 * @param pname         [IN] pointer to the name of the object in the destination directory.
 * @param type          [IN] type of the object to be created.
 * @param mode          [IN] mode to be used at file creation
 * @param openflags     [IN] flags to be used during file open
 * @param pcreate_arg   [IN] additional argument for object creation
 * @param pattr         [OUT] attributes for the new object.
 * @param ht            [INOUT] hash table used for the cache.
 * @param pclient       [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext      [IN] FSAL credentials
 * @param pstatus       [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry\n
 * @return CACHE_INODE_BAD_TYPE either source or destination have incorrect type\n
 * @return CACHE_INODE_ENTRY_EXISTS entry of that name already exists in destination.
 *
 */
cache_entry_t *cache_inode_create_open(cache_entry_t * pentry_parent,
                                       fsal_name_t * pname,
                                       cache_inode_file_type_t type,
                                       fsal_accessmode_t mode,
                                       cache_inode_create_arg_t * pcreate_arg,
                                       fsal_openflags_t openflags,
                                       fsal_attrib_list_t * pattr,
                                       hash_table_t * ht,
                                       cache_inode_client_t * pclient,
                                       fsal_op_context_t * pcontext,
                                       cache_inode_status_t * pstatus)
{
  return NULL;
}                               /* cache_inode_create */
