/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    nfs4_op_setclientid.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:52 $
 * \version $Revision: 1.9 $
 * \brief   Routines used for managing the NFS4_OP_SETCLIENTID operation.
 *
 * nfs4_op_setclientid.c :  Routines used for managing the NFS4_OP_SETCLIENTID operation.
 * 
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "sal_functions.h"

/**
 *
 * nfs4_op_setclientid:  The NFS4_OP_SETCLIENTID operation.
 *
 * Gets the currentFH for the current compound requests.
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

int nfs4_op_setclientid(struct nfs_argop4 *op,
                        compound_data_t * data, struct nfs_resop4 *resp)
{
  return NFS4_OK;
}                               /* nfs4_op_setclientid */

/**
 * nfs4_op_setclientid_Free: frees what was allocared to handle nfs4_op_setclientid.
 * 
 * Frees what was allocared to handle nfs4_op_setclientid.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_setclientid_Free(SETCLIENTID4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_setclientid_Free */
