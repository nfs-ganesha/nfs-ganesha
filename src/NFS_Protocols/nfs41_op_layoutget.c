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
 * \file    nfs41_op_layoutget.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs41_op_lock.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_file_handle.h"
#include "nfs_tools.h"

/**
 * 
 * nfs41_op_layoutget: The NFS4_OP_LAYOUTGET operation. 
 *
 * This function implements the NFS4_OP_LAYOUTGET operation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see all the nfs41_op_<*> function
 * @see nfs4_Compound
 *
 */

#define arg_LAYOUTGET4 op->nfs_argop4_u.oplayoutget
#define res_LAYOUTGET4 resp->nfs_resop4_u.oplayoutget

int nfs41_op_layoutget(struct nfs_argop4 *op, compound_data_t * data,
                       struct nfs_resop4 *resp)
{
  cache_inode_state_data_t candidate_data;
  cache_inode_state_type_t candidate_type;
  cache_inode_state_t *file_state = NULL;
  cache_inode_status_t cache_status;
  cache_inode_state_t *pstate_exists = NULL;
  nfsv4_1_file_layout4 *pfile_layout = NULL;
  nfs_fh4 *pnfsfh4 = NULL;
  int rc;

  char __attribute__ ((__unused__)) funcname[] = "nfs41_op_layoutget";

  /* Lock are not supported */
  resp->resop = NFS4_OP_LAYOUTGET;

#ifndef _USE_PNFS
  res_LAYOUTGET4.logr_status = NFS4ERR_NOTSUPP;
  return res_LAYOUTGET4.logr_status;
#else

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_LAYOUTGET4.logr_status = NFS4ERR_NOFILEHANDLE;
      return res_LAYOUTGET4.logr_status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_LAYOUTGET4.logr_status = NFS4ERR_BADHANDLE;
      return res_LAYOUTGET4.logr_status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_LAYOUTGET4.logr_status = NFS4ERR_FHEXPIRED;
      return res_LAYOUTGET4.logr_status;
    }

  /* Commit is done only on a file */
  if(data->current_filetype != REGULAR_FILE)
    {
      /* Type of the entry is not correct */
      switch (data->current_filetype)
        {
        case DIR_BEGINNING:
        case DIR_CONTINUE:
          res_LAYOUTGET4.logr_status = NFS4ERR_ISDIR;
          break;
        default:
          res_LAYOUTGET4.logr_status = NFS4ERR_INVAL;
          break;
        }

      return res_LAYOUTGET4.logr_status;
    }

  /* Parameters's consistency */
  if(arg_LAYOUTGET4.loga_length < arg_LAYOUTGET4.loga_minlength)
    {
      res_LAYOUTGET4.logr_status = NFS4ERR_INVAL;
      return res_LAYOUTGET4.logr_status;
    }

  /* Check stateid correctness */
  if((rc = nfs4_Check_Stateid(&arg_LAYOUTGET4.loga_stateid,
                              data->current_entry, data->psession->clientid)) != NFS4_OK)
    {
      res_LAYOUTGET4.logr_status = rc;
      return res_LAYOUTGET4.logr_status;
    }

  /* For the moment, only LAYOUT4_FILE is supported */
  switch (arg_LAYOUTGET4.loga_layout_type)
    {
    case LAYOUT4_NFSV4_1_FILES:
      /* Continue on proceeding the request */
      break;

    default:
      res_LAYOUTGET4.logr_status = NFS4ERR_NOTSUPP;
      return res_LAYOUTGET4.logr_status;
      break;
    }                           /* switch( arg_LAYOUTGET4.loga_layout_type ) */

  /* Get the related powner (from a previously made call to OPEN) */
  if(cache_inode_get_state(arg_LAYOUTGET4.loga_stateid.other,
                           &pstate_exists,
                           data->pclient, &cache_status) != CACHE_INODE_SUCCESS)
    {
      if(cache_status == CACHE_INODE_NOT_FOUND)
        res_LAYOUTGET4.logr_status = NFS4ERR_STALE_STATEID;
      else
        res_LAYOUTGET4.logr_status = NFS4ERR_INVAL;

      return res_LAYOUTGET4.logr_status;
    }

  /* Add a pstate */
  candidate_type = CACHE_INODE_STATE_LAYOUT;
  candidate_data.layout.layout_type = arg_LAYOUTGET4.loga_layout_type;
  candidate_data.layout.iomode = arg_LAYOUTGET4.loga_iomode;
  candidate_data.layout.offset = arg_LAYOUTGET4.loga_offset;
  candidate_data.layout.length = arg_LAYOUTGET4.loga_length;
  candidate_data.layout.minlength = arg_LAYOUTGET4.loga_minlength;

  /* Add the lock state to the lock table */
  if(cache_inode_add_state(data->current_entry,
                           candidate_type,
                           &candidate_data,
                           pstate_exists->powner,
                           data->pclient,
                           data->pcontext,
                           &file_state, &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_LAYOUTGET4.logr_status = NFS4ERR_STALE_STATEID;
      return res_LAYOUTGET4.logr_status;
    }

  /* set the returned status */

  /* No return on close for the moment */
  res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_return_on_close = FALSE;

  /* Manages the stateid */
  res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_stateid.seqid = 1;
  memcpy(res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_stateid.other,
         file_state->stateid_other, 12);

  /* Now the layout specific information */
  res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_len = 1;  /** @todo manages more than one segment */
  res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val =
      (layout4 *) Mem_Alloc(sizeof(layout4));

  res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].lo_offset =
      arg_LAYOUTGET4.loga_offset;
  res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].lo_length = 0xFFFFFFFFLL;   /* Whole file */
  res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].lo_iomode =
      arg_LAYOUTGET4.loga_iomode;
  res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].lo_content.
      loc_type = LAYOUT4_NFSV4_1_FILES;
  res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].lo_content.
      loc_body.loc_body_len = sizeof(nfsv4_1_file_layout4);
  res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].lo_content.
      loc_body.loc_body_val = Mem_Alloc(sizeof(nfsv4_1_file_layout4));
  pfile_layout =
      (nfsv4_1_file_layout4 *) res_LAYOUTGET4.LAYOUTGET4res_u.logr_resok4.logr_layout.
      logr_layout_val[0].lo_content.loc_body.loc_body_val;
  memset(pfile_layout->nfl_deviceid, 0, NFS4_DEVICEID4_SIZE);
  pfile_layout->nfl_deviceid[0] = 1;
  pfile_layout->nfl_util = 0x2000;  /** @TODO do not know why I should set this value */
  pfile_layout->nfl_first_stripe_index = 0;
  pfile_layout->nfl_pattern_offset = 0;
  pfile_layout->nfl_fh_list.nfl_fh_list_len = htonl( 1 ); /* Without htol, xdr within NFSv4 will be quite unhappy... */
  pfile_layout->nfl_fh_list.nfl_fh_list_val = (nfs_fh4 *)Mem_Alloc( sizeof( nfs_fh4 ) ) ;
  pfile_layout->nfl_fh_list.nfl_fh_list_val[0].nfs_fh4_len =  /* Same thing as stated above */
      htonl( data->current_entry->object.file.pnfs_file.ds_file.handle.nfs_fh4_len ) ;
  pfile_layout->nfl_fh_list.nfl_fh_list_val[0].nfs_fh4_val =
      data->current_entry->object.file.pnfs_file.ds_file.handle.nfs_fh4_val;

  res_LAYOUTGET4.logr_status = NFS4_OK;
  return res_LAYOUTGET4.logr_status;
#endif                          /* _USE_PNFS */
}                               /* nfs41_op_layoutget */

/**
 * nfs41_op_layoutget_Free: frees what was allocared to handle nfs41_op_layoutget.
 * 
 * Frees what was allocared to handle nfs41_op_layoutget.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_layoutget_Free(LAYOUTGET4res * resp)
{
  if(resp->logr_status == NFS4_OK)
    {
      Mem_Free((char *)resp->LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val);
      Mem_Free((char *)resp->LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].
               lo_content.loc_body.loc_body_val);
    }

  return;
}                               /* nfs41_op_layoutget_Free */
