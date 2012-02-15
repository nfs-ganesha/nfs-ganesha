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
 * \file    nfs4_lease.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/20 07:39:22 $
 * \version $Revision: 1.43 $
 * \brief   Some tools very usefull in the nfs4 protocol implementation.
 *
 * nfs4_lease.c : Some functions to manage NFSv4 leases
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/MainNFSD/nfs_tools.c,v 1.43 2006/01/20 07:39:22 leibovic Exp $
 *
 * $Log$
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "log.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"

/**
 *
 * nfs4_is_leased_expired
 *
 * This routine checks if the client's lease has expired.
 *
 * @param pclient [IN] pointer to the client to be checked.
 *
 * @return 1 if expired, 0 otherwise.
 *
 */
int nfs4_is_lease_expired(nfs_client_id_t * clientp)
{
  LogFullDebug(COMPONENT_NFS_V4,
               "Check lease for client_name = %s id=%"PRIx64"",
               clientp->client_name, clientp->clientid);

  LogFullDebug(COMPONENT_NFS_V4,
               "--------- nfs4_is_lease_expired ---------> %lu %lu delta=%lu lease=%u",
               time(NULL), clientp->last_renew,
               time(NULL) - clientp->last_renew, nfs_param.nfsv4_param.lease_lifetime);

  /* Check is lease is still valid */
  if(time(NULL) - clientp->last_renew > (int)nfs_param.nfsv4_param.lease_lifetime)
    return 1;
  else
    return 0;
}                               /* nfs4_is_lease_expired */

void nfs4_update_lease(nfs_client_id_t * clientp)
{
  LogFullDebug(COMPONENT_NFS_V4,
               "Update lease for client_name = %s id=%"PRIx64"",
               clientp->client_name, clientp->clientid);

  clientp->last_renew = time(NULL);
}

