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
 * \file    nfs4_op_lockt.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_lockt.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include <sys/file.h>		/* for having FNDELAY */
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
 * nfs4_op_lockt: The NFS4_OP_LOCKT operation. 
 *
 * This function implements the NFS4_OP_LOCKT operation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see all the nfs4_op_<*> function
 * @see nfs4_Compound
 *
 */

#define arg_LOCKT4 op->nfs_argop4_u.oplockt
#define res_LOCKT4 resp->nfs_resop4_u.oplockt

int nfs4_op_lockt(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_lockt";

  cache_inode_status_t cache_status;
  nfs_client_id_t nfs_client_id;
  cache_inode_state_t *pstate_found = NULL;
  uint64_t a, b, a1, b1;
  unsigned int overlap = FALSE;

  /* Lock are not supported */
  resp->resop = NFS4_OP_LOCKT;

#ifndef _WITH_NFSV4_LOCKS
  res_LOCKT4.status = NFS4ERR_LOCK_NOTSUPP;
  return res_LOCKT4.status;
#else

  /* If there is no FH */
  if (nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_LOCKT4.status = NFS4ERR_NOFILEHANDLE;
      return res_LOCKT4.status;
    }

  /* If the filehandle is invalid */
  if (nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_LOCKT4.status = NFS4ERR_BADHANDLE;
      return res_LOCKT4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if (nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_LOCKT4.status = NFS4ERR_FHEXPIRED;
      return res_LOCKT4.status;
    }

  /* Commit is done only on a file */
  if (data->current_filetype != REGULAR_FILE)
    {
      /* Type of the entry is not correct */
      switch (data->current_filetype)
	{
	case DIR_BEGINNING:
	case DIR_CONTINUE:
	  res_LOCKT4.status = NFS4ERR_ISDIR;
	  break;
	default:
	  res_LOCKT4.status = NFS4ERR_INVAL;
	  break;
	}
      return res_LOCKT4.status;
    }

  /* Lock length should not be 0 */
  if (arg_LOCKT4.length == 0LL)
    {
      res_LOCKT4.status = NFS4ERR_INVAL;
      return res_LOCKT4.status;
    }

  /* Check for range overflow 
   * Remember that a length with all bits set to 1 means "lock until the end of file" (RFC3530, page 157) */
  if (arg_LOCKT4.length != 0xffffffffffffffffLL)
    {
      /* Comparing beyond 2^64 is not possible int 64 bits precision, 
       * but off+len > 2^64 is equivalent to len > 2^64 - off */
      if (arg_LOCKT4.length > (0xffffffffffffffffLL - arg_LOCKT4.offset))
	{
	  res_LOCKT4.status = NFS4ERR_INVAL;
	  return res_LOCKT4.status;
	}
    }

  /* Check clientid */
  if (nfs_client_id_get(arg_LOCKT4.owner.clientid, &nfs_client_id) != CLIENT_ID_SUCCESS)
    {
      res_LOCKT4.status = NFS4ERR_STALE_CLIENTID;
      return res_LOCKT4.status;
    }

  /* loop into the states related to this pentry to find the related lock */
  pstate_found = NULL;
  do
    {
      cache_inode_state_iterate(data->current_entry,
				&pstate_found,
				pstate_found,
				data->pclient, data->pcontext, &cache_status);
      if ((cache_status == CACHE_INODE_STATE_ERROR)
	  || (cache_status == CACHE_INODE_INVALID_ARGUMENT))
	{
	  res_LOCKT4.status = NFS4ERR_INVAL;
	  return res_LOCKT4.status;
	}

      if (pstate_found != NULL)
	{
	  if (pstate_found->state_type == CACHE_INODE_STATE_LOCK)
	    {

	      /* We found a lock, check is they overlap */
	      a = pstate_found->state_data.lock.offset;
	      b = pstate_found->state_data.lock.offset +
		  pstate_found->state_data.lock.length;
	      a1 = arg_LOCKT4.offset;
	      b1 = arg_LOCKT4.offset + arg_LOCKT4.length;

	      /* Locks overlap is a <= a1 < b or a < b1 <= b */
	      overlap = FALSE;
	      if (a <= a1)
		{
		  if (a1 < b)
		    overlap = TRUE;
		} else
		{
		  if (a < b1)
		    {
		      if (b1 <= b)
			overlap = TRUE;
		    }
		}

	      if (overlap == TRUE)
		{
		  if ((arg_LOCKT4.locktype != READ_LT)
		      || (pstate_found->state_data.lock.lock_type != READ_LT))
		    {
		      /* Overlapping lock is found, if owner is different than the calling owner, return NFS4ERR_DENIED */
		      if ((arg_LOCKT4.owner.owner.owner_len ==
			   pstate_found->powner->owner_len)
			  &&
			  (!memcmp
			   (arg_LOCKT4.owner.owner.owner_val,
			    pstate_found->powner->owner_val,
			    pstate_found->powner->owner_len)))
			{
			  /* The calling state owner is the same. There is a discussion on this case at page 161 of RFC3530. I choose to ignore this
			   * lock and continue iterating on the other states */
			} else
			{
			  /* A  conflicting lock from a different lock_owner, returns NFS4ERR_DENIED */
			  res_LOCKT4.LOCKT4res_u.denied.offset =
			      pstate_found->state_data.lock.offset;
			  res_LOCKT4.LOCKT4res_u.denied.length =
			      pstate_found->state_data.lock.length;
			  res_LOCKT4.LOCKT4res_u.denied.locktype =
			      pstate_found->state_data.lock.lock_type;
			  res_LOCKT4.LOCKT4res_u.denied.owner.owner.owner_len =
			      pstate_found->powner->owner_len;
			  res_LOCKT4.LOCKT4res_u.denied.owner.owner.owner_val =
			      pstate_found->powner->owner_val;
			  res_LOCKT4.status = NFS4ERR_DENIED;
			  return res_LOCKT4.status;
			}
		    }
		}
	    }
	}
    }
  while (pstate_found != NULL);

  /* Succssful exit, no conflicting lock were found */
  res_LOCKT4.status = NFS4_OK;
  return res_LOCKT4.status;

#endif
}				/* nfs4_op_lockt */

/**
 * nfs4_op_lockt_Free: frees what was allocared to handle nfs4_op_lockt.
 * 
 * Frees what was allocared to handle nfs4_op_lockt.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_lockt_Free(LOCKT4res * resp)
{
  /* Nothing to Mem_Free */
  return;
}				/* nfs4_op_lockt_Free */
