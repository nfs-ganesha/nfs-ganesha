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
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"

static unsigned int _valid_lease(nfs_client_id_t * pclientid)
{
  time_t t;

  if(pclientid->cid_confirmed == EXPIRED_CLIENT_ID)
    return 0;

  if(pclientid->cid_lease_reservations != 0)
    return nfs_param.nfsv4_param.lease_lifetime;

  t = time(NULL);

  if(pclientid->cid_last_renew + nfs_param.nfsv4_param.lease_lifetime > t)
    return (pclientid->cid_last_renew + nfs_param.nfsv4_param.lease_lifetime) - t;

  return 0;
}

/**
 *
 *  valid_lease: Check if lease is valid, caller holds cid_mutex.
 *
 * Check if lease is valid, caller holds cid_mutex.
 *
 * @param pclientid [IN] clientid record to check lease for.
 *
 * @return 1 if lease is valid, 0 if not.
 *
 */
int valid_lease(nfs_client_id_t * pclientid)
{
  unsigned int valid;

  valid = _valid_lease(pclientid);

  if(isFullDebug(COMPONENT_CLIENTID))
    {
      char                  str[LOG_BUFF_LEN];
      struct display_buffer dspbuf = {sizeof(str), str, str};

      (void) display_client_id_rec(&dspbuf, pclientid);

      LogFullDebug(COMPONENT_CLIENTID,
                   "Check Lease %s (Valid=%s %u seconds left)",
                   str, valid ? "YES" : "NO", valid);
    }

  return valid != 0;
}

/**
 *
 *  reserve_lease_lock: Check if lease is valid and reserve it and retain cid_mutex.
 *
 * Check if lease is valid and reserve it and retain cid_mutex.
 *
 * Lease reservation prevents any other thread from expiring the lease. Caller
 * must call update lease to release the reservation.
 *
 * @param pclientid [IN] clientid record to check lease for.
 *
 * @return 1 if lease is valid, 0 if not.
 *
 */
int reserve_lease(nfs_client_id_t * pclientid)
{
  unsigned int valid;

  valid = _valid_lease(pclientid);

  if(valid != 0)
    pclientid->cid_lease_reservations++;

  if(isFullDebug(COMPONENT_CLIENTID))
    {
      char                  str[LOG_BUFF_LEN];
      struct display_buffer dspbuf = {sizeof(str), str, str};

      (void) display_client_id_rec(&dspbuf, pclientid);

      LogFullDebug(COMPONENT_CLIENTID,
                   "Reserve Lease %s (Valid=%s %u seconds left)",
                   str, valid ? "YES" : "NO", valid);
    }

  return valid != 0;
}

/**
 *
 * update_lease: Release a lease reservation, and update lease.
 *
 * Release a lease reservation, and update lease. Caller must hold cid_mutex.
 *
 * Lease reservation prevents any other thread from expiring the lease. This
 * function releases the lease reservation. Before releasing the last
 * reservation, cid_last_renew will be updated.
 *
 * @param pclientid [IN] clientid record to check lease for.
 *
 * @return 1 if lease is valid, 0 if not.
 *
 */
void update_lease(nfs_client_id_t * pclientid)
{
  pclientid->cid_lease_reservations--;

  /* Renew lease when last reservation is released */
  if(pclientid->cid_lease_reservations == 0)
    pclientid->cid_last_renew = time(NULL);

  if(isFullDebug(COMPONENT_CLIENTID))
    {
      char                  str[LOG_BUFF_LEN];
      struct display_buffer dspbuf = {sizeof(str), str, str};

      (void) display_client_id_rec(&dspbuf, pclientid);

      LogFullDebug(COMPONENT_CLIENTID,
                   "Update Lease %s",
                   str);
    }
}
