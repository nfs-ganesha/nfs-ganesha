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
 * \file    nfs4_op_setclientid_confirm.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:52 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4_OP_SETCLIENTID_CONFIRM operation.
 *
 * nfs4_op_setclientid_confirm.c :  Routines used for managing the NFS4_OP_SETCLIENTID_CONFIRM operation.
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

extern time_t ServerBootTime;

/**
 *
 * nfs4_op_setclientid_confirm:  The NFS4_OP_SETCLIENTID_CONFIRM operation.
 *
 * The NFS4_OP_SETCLIENTID_CONFIRM operation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_setclientid_confirm(struct nfs_argop4 *op,
				compound_data_t * data, struct nfs_resop4 *resp)
{
  nfs_client_id_t nfs_clientid;
  clientid4 clientid = 0;
  nfs_worker_data_t *pworker = NULL;

  pworker = (nfs_worker_data_t *) data->pclient->pworker;

#define arg_SETCLIENTID_CONFIRM4 op->nfs_argop4_u.opsetclientid_confirm
#define res_SETCLIENTID_CONFIRM4 resp->nfs_resop4_u.opsetclientid_confirm

  resp->resop = NFS4_OP_SETCLIENTID_CONFIRM;
  res_SETCLIENTID_CONFIRM4.status = NFS4_OK;
  clientid = arg_SETCLIENTID_CONFIRM4.clientid;

  DisplayLogLevel(NIV_DEBUG, "SETCLIENTID_CONFIRM clientid = %llx", clientid);
  /* DisplayLogLevel( NIV_DEBUG, "SETCLIENTID_CONFIRM Verifier = #%s#", arg_SETCLIENTID_CONFIRM4.setclientid_confirm ) ; */

  /* Does this id already exists ? */
  if (nfs_client_id_get(clientid, &nfs_clientid) == CLIENT_ID_SUCCESS)
    {
      /* The client id should not be confirmed */
      if (nfs_clientid.confirmed == CONFIRMED_CLIENT_ID)
	{
	  /* Client id was already confirmed and is then in use, this is NFS4ERR_CLID_INUSE if not same client */

	  /* Check the verifier */
	  if (strncmp
	      (nfs_clientid.verifier, arg_SETCLIENTID_CONFIRM4.setclientid_confirm,
	       NFS4_VERIFIER_SIZE))
	    {
	      /* Bad verifier */
	      res_SETCLIENTID_CONFIRM4.status = NFS4ERR_CLID_INUSE;
	      return res_SETCLIENTID_CONFIRM4.status;
	    }
	} else
	{
	  if (nfs_clientid.confirmed == REBOOTED_CLIENT_ID)
	    {
	      DisplayLogLevel(NIV_DEBUG,
			      "SETCLIENTID_CONFIRM clientid = %llx, client was rebooted, getting ride of old state from previous client instance",
			      clientid);
	    }

	  /* Regular situation, set the client id confirmed and returns */
	  nfs_clientid.confirmed = CONFIRMED_CLIENT_ID;

	  /* Set the time for the client id */
	  nfs_clientid.last_renew = time(NULL);

	  /* Set the new value */
	  if (nfs_client_id_set(clientid, nfs_clientid, pworker->clientid_pool) !=
	      CLIENT_ID_SUCCESS)
	    {
	      res_SETCLIENTID_CONFIRM4.status = NFS4ERR_SERVERFAULT;
	      return res_SETCLIENTID_CONFIRM4.status;
	    }
	}
    } else
    {
      /* The client id does not exist: stale client id */
      res_SETCLIENTID_CONFIRM4.status = NFS4ERR_STALE_CLIENTID;
      return res_SETCLIENTID_CONFIRM4.status;
    }

  /* Successful exit */
  res_SETCLIENTID_CONFIRM4.status = NFS4_OK;
  return res_SETCLIENTID_CONFIRM4.status;
}				/* nfs4_op_setclientid_confirm */

/**
 * nfs4_op_setclientid_confirm_Free: frees what was allocared to handle nfs4_op_setclientid_confirm.
 * 
 * Frees what was allocared to handle nfs4_op_setclientid_confirm.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_setclientid_confirm_Free(SETCLIENTID_CONFIRM4res * resp)
{
  /* To be completed */
  return;
}				/* nfs4_op_setclientid_confirm_Free */
