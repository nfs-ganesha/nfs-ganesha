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
 * \file    nfs4_op_write.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:52 $
 * \version $Revision: 1.15 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_write.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "cache_content_policy.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"

extern nfs_parameter_t nfs_param;

/**
 * nfs4_op_write: The NFS4_OP_WRITE operation
 * 
 * This functions handles the NFS4_OP_WRITE operation in NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */

extern verifier4 NFS4_write_verifier;   /* NFS V4 write verifier from nfs_Main.c     */

#define arg_WRITE4 op->nfs_argop4_u.opwrite
#define res_WRITE4 resp->nfs_resop4_u.opwrite

extern char all_zero[];
extern char all_one[12];

int nfs4_op_write(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_write";

  fsal_seek_t seek_descriptor;
  fsal_size_t size;
  fsal_size_t written_size;
  fsal_off_t offset;
  fsal_boolean_t eof_met;
  bool_t stable_flag = TRUE;
  caddr_t bufferdata;
  stable_how4 stable_how;
  cache_content_status_t content_status;
  cache_inode_state_t *pstate_found = NULL;
  cache_inode_status_t cache_status;
  fsal_attrib_list_t attr;
  cache_entry_t *entry = NULL;
  cache_inode_state_t *pstate_iterate = NULL;
  cache_inode_state_t *pstate_previous_iterate = NULL;

  int rc = 0;

  cache_content_policy_data_t datapol;

  datapol.UseMaxCacheSize = FALSE;

  /* Lock are not supported */
  resp->resop = NFS4_OP_WRITE;
  res_WRITE4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_WRITE4.status = NFS4ERR_NOFILEHANDLE;
      return res_WRITE4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_WRITE4.status = NFS4ERR_BADHANDLE;
      return res_WRITE4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_WRITE4.status = NFS4ERR_FHEXPIRED;
      return res_WRITE4.status;
    }

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_write_xattr(op, data, resp);

  /* Manage access type MDONLY */
  if(data->pexport->access_type == ACCESSTYPE_MDONLY)
    {
      res_WRITE4.status = NFS4ERR_DQUOT;
      return res_WRITE4.status;
    }

  /* vnode to manage is the current one */
  entry = data->current_entry;

  /* Check for special stateid */
  if(!memcmp((char *)all_zero, arg_WRITE4.stateid.other, 12) &&
     arg_WRITE4.stateid.seqid == 0)
    {
      /* "All 0 stateid special case", see RFC3530 page 220-221 for details 
       * This will be treated as a client that held no lock at all,
       * I set pstate_found to NULL to remember this situation later */
      pstate_found = NULL;
    }
  else if(!memcmp((char *)all_one, arg_WRITE4.stateid.other, 12) &&
          arg_WRITE4.stateid.seqid == 0xFFFFFFFF)
    {
      /* "All 1 stateid special case", see RFC3530 page 220-221 for details 
       * This will be treated as a client that held no lock at all,
       * I set pstate_found to NULL to remember this situation later */
      pstate_found = NULL;
    }
  /* Check for correctness of the provided stateid */
  else if((rc = nfs4_Check_Stateid(&arg_WRITE4.stateid, data->current_entry, 0LL)) ==
          NFS4_OK)
    {
      /* Get the related state */
      if(cache_inode_get_state(arg_WRITE4.stateid.other,
                               &pstate_found,
                               data->pclient, &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_WRITE4.status = nfs4_Errno(cache_status);
          return res_WRITE4.status;
        }

      /* This is a read operation, this means that the file MUST have been opened for reading */
      if((pstate_found->state_data.share.share_deny & OPEN4_SHARE_DENY_WRITE) &&
         !(pstate_found->state_data.share.share_access & OPEN4_SHARE_ACCESS_WRITE))
        {
          /* Bad open mode, return NFS4ERR_OPENMODE */
          res_WRITE4.status = NFS4ERR_OPENMODE;
          return res_WRITE4.status;
        }
#ifdef _TOTO
      /* Check the seqid */
      if((arg_WRITE4.stateid.seqid != pstate_found->powner->seqid) &&
         (arg_WRITE4.stateid.seqid != pstate_found->powner->seqid + 1))
        {
          res_WRITE4.status = NFS4ERR_BAD_SEQID;
          return res_WRITE4.status;
        }

      /* If NFSv4::Use_OPEN_CONFIRM is set to TRUE in the configuration file, check is state is confirmed */
      if(nfs_param.nfsv4_param.use_open_confirm == TRUE)
        {
          if(pstate_found->powner->confirmed == FALSE)
            {
              res_WRITE4.status = NFS4ERR_BAD_STATEID;
              return res_WRITE4.status;
            }
        }
#endif
    }                           /* else if( ( rc = nfs4_Check_Stateid( &arg_WRITE4.stateid, data->current_entry ) ) == NFS4_OK ) */
  else
    {
      res_WRITE4.status = rc;
      return res_WRITE4.status;
    }

  /* NB: After this points, if pstate_found == NULL, then the stateid is all-0 or all-1 */

  /* Iterate through file's state to look for conflicts */
  pstate_iterate = NULL;
  pstate_previous_iterate = NULL;
  do
    {
      cache_inode_state_iterate(data->current_entry,
                                &pstate_iterate,
                                pstate_previous_iterate,
                                data->pclient, data->pcontext, &cache_status);
      if(cache_status == CACHE_INODE_STATE_ERROR)
        break;                  /* Get out of the loop */

      if(cache_status == CACHE_INODE_INVALID_ARGUMENT)
        {
          res_WRITE4.status = NFS4ERR_INVAL;
          return res_WRITE4.status;
        }

      if(pstate_iterate != NULL)
        {
          switch (pstate_iterate->state_type)
            {
            case CACHE_INODE_STATE_SHARE:
              if(pstate_found != pstate_iterate)
                {
                  if(pstate_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_WRITE)
                    {
                      /* Writing to this file if prohibited, file is write-denied */
                      res_WRITE4.status = NFS4ERR_LOCKED;
                      return res_WRITE4.status;
                    }
                }
              break;
            }
        }
      pstate_previous_iterate = pstate_iterate;
    }
  while(pstate_iterate != NULL);

  /* Only files can be written */
  if(data->current_filetype != REGULAR_FILE)
    {
      /* If the destination is no file, return EISDIR if it is a directory and EINAVL otherwise */
      if(data->current_filetype == DIR_BEGINNING
         || data->current_filetype == DIR_CONTINUE)
        res_WRITE4.status = NFS4ERR_ISDIR;
      else
        res_WRITE4.status = NFS4ERR_INVAL;

      return res_WRITE4.status;
    }

  /* Get the characteristics of the I/O to be made */
  offset = arg_WRITE4.offset;
  size = arg_WRITE4.data.data_len;
  stable_how = arg_WRITE4.stable;
#ifdef _DEBUG_NFS_V4
  printf("   NFS4_OP_WRITE: offset = %llu  length = %llu   stable = %d\n", offset, size,
         stable_how);
#endif

  if((data->pexport->options & EXPORT_OPTION_MAXOFFSETWRITE) ==
     EXPORT_OPTION_MAXOFFSETWRITE)
    if((fsal_off_t) (offset + size) > data->pexport->MaxOffsetWrite)
      {
        res_WRITE4.status = NFS4ERR_DQUOT;
        return res_WRITE4.status;
      }

  /* The size to be written should not be greater than FATTR4_MAXWRITESIZE because this value is asked 
   * by the client at mount time, but we check this by security */
  if((data->pexport->options & EXPORT_OPTION_MAXWRITE == EXPORT_OPTION_MAXWRITE) &&
     size > data->pexport->MaxWrite)
    {
      /*
       * The client asked for too much data, we
       * must restrict him 
       */
      size = data->pexport->MaxWrite;
    }

  /* Where are the data ? */
  bufferdata = arg_WRITE4.data.data_val;

#ifdef _DEBUG_NFS_V4
  printf("             NFS4_OP_WRITE: offset = %llu  length = %llu\n", offset, size);
#endif

  /* if size == 0 , no I/O) are actually made and everything is alright */
  if(size == 0)
    {
      res_WRITE4.WRITE4res_u.resok4.count = 0;
      res_WRITE4.WRITE4res_u.resok4.committed = FILE_SYNC4;

      memcpy(res_WRITE4.WRITE4res_u.resok4.writeverf, NFS4_write_verifier,
             sizeof(verifier4));

      res_WRITE4.status = NFS4_OK;
      return res_WRITE4.status;
    }

  if((data->pexport->options & EXPORT_OPTION_USE_DATACACHE) &&
     (cache_content_cache_behaviour(entry,
                                    &datapol,
                                    (cache_content_client_t *) (data->
                                                                pclient->pcontent_client),
                                    &content_status) == CACHE_CONTENT_FULLY_CACHED)
     && (entry->object.file.pentry_content == NULL))
    {
      /* Entry is not in datacache, but should be in, cache it .
       * Several threads may call this function at the first time and a race condition can occur here
       * in order to avoid this, cache_inode_add_data_cache is "mutex protected" 
       * The first call will create the file content cache entry, the further will return
       * with error CACHE_INODE_CACHE_CONTENT_EXISTS which is not a pathological thing here */

      datapol.UseMaxCacheSize = data->pexport->options & EXPORT_OPTION_MAXCACHESIZE;
      datapol.MaxCacheSize = data->pexport->MaxCacheSize;

      /* Status is set in last argument */
      cache_inode_add_data_cache(entry, data->ht, data->pclient, data->pcontext,
                                 &cache_status);

      if((cache_status != CACHE_INODE_SUCCESS) &&
         (cache_status != CACHE_INODE_CACHE_CONTENT_EXISTS))
        {
          res_WRITE4.status = NFS4ERR_SERVERFAULT;
          return res_WRITE4.status;
        }

    }

  if((nfs_param.core_param.use_nfs_commit == TRUE) && (arg_WRITE4.stable == UNSTABLE4))
    {
      stable_flag = FALSE;
    }
  else
    {
      stable_flag = TRUE;
    }

  /* An actual write is to be made, prepare it */
  /* only FILE_SYNC mode is supported */
  /* Set up uio to define the transfer */
  seek_descriptor.whence = FSAL_SEEK_SET;
  seek_descriptor.offset = offset;

  if(cache_inode_rdwr(entry,
                      CACHE_CONTENT_WRITE,
                      &seek_descriptor,
                      size,
                      &written_size,
                      &attr,
                      bufferdata,
                      &eof_met,
                      data->ht,
                      data->pclient,
                      data->pcontext, stable_flag, &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_WRITE4.status = nfs4_Errno(cache_status);
      return res_WRITE4.status;
    }

  /* Set the returned value */
  if(stable_flag == TRUE)
    res_WRITE4.WRITE4res_u.resok4.committed = FILE_SYNC4;
  else
    res_WRITE4.WRITE4res_u.resok4.committed = UNSTABLE4;

  res_WRITE4.WRITE4res_u.resok4.count = written_size;
  memcpy(res_WRITE4.WRITE4res_u.resok4.writeverf, NFS4_write_verifier, sizeof(verifier4));

  res_WRITE4.status = NFS4_OK;

  return res_WRITE4.status;
}                               /* nfs4_op_write */

/**
 * nfs4_op_write_Free: frees what was allocared to handle nfs4_op_write.
 * 
 * Frees what was allocared to handle nfs4_op_write.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_write_Free(WRITE4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_write_Free */
