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
 * \file    nfs4_op_setattr.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:52 $
 * \version $Revision: 1.15 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_setattr.c : Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"

/**
 * nfs4_op_rename: The NFS4_OP_SETATTR operation.
 * 
 * This functions handles the NFS4_OP_SETATTR operation in NFSv4. This function can be called only from nfs4_Compound
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */

#define arg_SETATTR4 op->nfs_argop4_u.opsetattr
#define res_SETATTR4 resp->nfs_resop4_u.opsetattr

int nfs4_op_setattr(struct nfs_argop4 *op,
                    compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_attrib_list_t sattr;
  fsal_attrib_list_t parent_attr;
  cache_inode_status_t cache_status;
  int rc = 0;
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_setattr";

  resp->resop = NFS4_OP_SETATTR;
  res_SETATTR4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_SETATTR4.status = NFS4ERR_NOFILEHANDLE;
      return res_SETATTR4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_SETATTR4.status = NFS4ERR_BADHANDLE;
      return res_SETATTR4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_SETATTR4.status = NFS4ERR_FHEXPIRED;
      return res_SETATTR4.status;
    }

  /* Pseudo Fs is explictely a Read-Only File system */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_SETATTR4.status = NFS4ERR_ROFS;
      return res_SETATTR4.status;
    }

  /* Get only attributes that are allowed to be read */
  if(!nfs4_Fattr_Check_Access(&arg_SETATTR4.obj_attributes, FATTR4_ATTR_WRITE))
    {
      res_SETATTR4.status = NFS4ERR_INVAL;
      return res_SETATTR4.status;
    }

  /* Ask only for supported attributes */
  if(!nfs4_Fattr_Supported(&arg_SETATTR4.obj_attributes))
    {
      res_SETATTR4.status = NFS4ERR_ATTRNOTSUPP;
      return res_SETATTR4.status;
    }

  /* Convert the fattr4 in the request to a nfs3_sattr structure */
  rc = nfs4_Fattr_To_FSAL_attr(&sattr, &(arg_SETATTR4.obj_attributes));

  if(rc == 0)
    {
      res_SETATTR4.status = NFS4ERR_ATTRNOTSUPP;
      return res_SETATTR4.status;
    }

  if(rc == -1)
    {
      res_SETATTR4.status = NFS4ERR_BADXDR;
      return res_SETATTR4.status;
    }

  /*
   * trunc may change Xtime so we have to start with trunc and finish
   * by the mtime and atime 
   */
  if(FSAL_TEST_MASK(sattr.asked_attributes, FSAL_ATTR_SIZE))
    {
      /* Setting the size of a directory is prohibited */
      if(data->current_filetype == DIR_BEGINNING
         || data->current_filetype == DIR_CONTINUE)
        {
          res_SETATTR4.status = NFS4ERR_ISDIR;
          return res_SETATTR4.status;
        }

      if((cache_status = cache_inode_truncate(data->current_entry,
                                              sattr.filesize,
                                              &parent_attr,
                                              data->ht,
                                              data->pclient,
                                              data->pcontext,
                                              &cache_status)) != CACHE_INODE_SUCCESS)
        {
          res_SETATTR4.status = nfs4_Errno(cache_status);
          return res_SETATTR4.status;
        }
    }

  /* Now, we set the mode */
  if(FSAL_TEST_MASK(sattr.asked_attributes, FSAL_ATTR_MODE) ||
     FSAL_TEST_MASK(sattr.asked_attributes, FSAL_ATTR_OWNER) ||
     FSAL_TEST_MASK(sattr.asked_attributes, FSAL_ATTR_GROUP) ||
     FSAL_TEST_MASK(sattr.asked_attributes, FSAL_ATTR_MTIME) ||
     FSAL_TEST_MASK(sattr.asked_attributes, FSAL_ATTR_ATIME))
    {
      /* Check for root access when using chmod */
      if(FSAL_TEST_MASK(sattr.asked_attributes, FSAL_ATTR_MODE))
        {
          if(((sattr.mode & 0x800) &&
              ((data->pexport->options & EXPORT_OPTION_NOSUID) == EXPORT_OPTION_NOSUID))
             || ((sattr.mode & 0x400)
                 && ((data->pexport->options & EXPORT_OPTION_NOSGID) ==
                     EXPORT_OPTION_NOSGID)))
            {
              res_SETATTR4.status = NFS4ERR_PERM;
              return res_SETATTR4.status;
            }
        }
#ifdef _TOTO
      /* get the current time */
      gettimeofday(&t, NULL);

      /* Set the atime and mtime (ctime is not setable) */
      /** @todo : check correctness of this block... looks suspicious */
      if(FSAL_TEST_MASK(sattr.asked_attributes, FSAL_ATTR_ATIME) == SET_TO_SERVER_TIME4)
        {
          sattr.atime.seconds = t.tv_sec;
          sattr.atime.nseconds = t.tv_usec;
        }

      /* Should we use the time from the client handside or from the server handside ? */
      /** @todo : check correctness of this block... looks suspicious */
      if(FSAL_TEST_MASK(sattr.asked_attributes, FSAL_ATTR_MTIME) == SET_TO_SERVER_TIME4)
        {
          sattr.mtime.seconds = t.tv_sec;
          sattr.mtime.nseconds = t.tv_usec;
        }
#endif

      if(cache_inode_setattr(data->current_entry,
                             &sattr,
                             data->ht,
                             data->pclient,
                             data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_SETATTR4.status = nfs4_Errno(cache_status);
          return res_SETATTR4.status;
        }
    }

  /* Set the replyed structure */
  res_SETATTR4.attrsset.bitmap4_len = arg_SETATTR4.obj_attributes.attrmask.bitmap4_len;

  if((res_SETATTR4.attrsset.bitmap4_val =
      (uint32_t *) Mem_Alloc(res_SETATTR4.attrsset.bitmap4_len * sizeof(u_int))) == NULL)
    {
      res_SETATTR4.status = NFS4ERR_SERVERFAULT;
      return res_SETATTR4.status;
    }
  memset((char *)res_SETATTR4.attrsset.bitmap4_val, 0,
         res_SETATTR4.attrsset.bitmap4_len * sizeof(u_int));

  memcpy(res_SETATTR4.attrsset.bitmap4_val,
         arg_SETATTR4.obj_attributes.attrmask.bitmap4_val,
         res_SETATTR4.attrsset.bitmap4_len * sizeof(u_int));

  /* Exit with no error */
  res_SETATTR4.status = NFS4_OK;

  return res_SETATTR4.status;
}                               /* nfs4_op_setattr */

/**
 * nfs4_op_setattr_Free: frees what was allocated to handle nfs4_op_setattr.
 * 
 * Frees what was allocared to handle nfs4_op_setattr.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_setattr_Free(SETATTR4res * resp)
{
  if(resp->status == NFS4_OK)
    Mem_Free(resp->attrsset.bitmap4_val);
  return;
}                               /* nfs4_op_setattr_Free */
