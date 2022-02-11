// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file  nfs4_lease.c
 * @brief NFSv4 lease management
 */

#include "config.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"

/**
 * @brief Return the lifetime of a valid lease
 *
 * @param[in] clientid The client record to check
 *
 * @return The lease lifetime or 0 if expired.
 */
static unsigned int _valid_lease(nfs_client_id_t *clientid)
{
	time_t t;

	if (clientid->cid_confirmed == EXPIRED_CLIENT_ID)
		return 0;

	if (clientid->cid_lease_reservations != 0)
		return nfs_param.nfsv4_param.lease_lifetime;

	t = time(NULL);

	if (clientid->cid_last_renew + nfs_param.nfsv4_param.lease_lifetime > t)
		return (clientid->cid_last_renew +
			nfs_param.nfsv4_param.lease_lifetime) - t;

	return 0;
}

/**
 * @brief Check if lease is valid
 *
 * The caller must hold cid_mutex.
 *
 * @param[in] clientid Record to check lease for.
 *
 * @return 1 if lease is valid, 0 if not.
 *
 */
bool valid_lease(nfs_client_id_t *clientid)
{
	unsigned int valid;

	valid = _valid_lease(clientid);

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_client_id_rec(&dspbuf, clientid);
		LogFullDebug(COMPONENT_CLIENTID,
			     "Check Lease %s (Valid=%s %u seconds left)", str,
			     valid ? "YES" : "NO", valid);
	}

	return valid != 0;
}

/**
 * @brief Check if lease is valid and reserve it and retain cid_mutex.
 *
 * Lease reservation prevents any other thread from expiring the lease. Caller
 * must call update lease to release the reservation.
 *
 * @param[in] clientid Client record to check lease for
 *
 * @return 1 if lease is valid, 0 if not.
 *
 */
int reserve_lease(nfs_client_id_t *clientid)
{
	unsigned int valid;

	valid = _valid_lease(clientid);

	if (valid != 0)
		clientid->cid_lease_reservations++;

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_client_id_rec(&dspbuf, clientid);
		LogFullDebug(COMPONENT_CLIENTID,
			     "Reserve Lease %s (Valid=%s %u seconds left)", str,
			     valid ? "YES" : "NO", valid);
	}

	return valid != 0;
}

/**
 * @brief Release a lease reservation and update lease.
 *
 * Lease reservation prevents any other thread from expiring the lease. This
 * function releases the lease reservation. Before releasing the last
 * reservation, cid_last_renew will be updated.
 *
 * @param[in] clientid The clientid record to update
 *
 * @return 1 if lease is valid, 0 if not.
 *
 */
void update_lease(nfs_client_id_t *clientid)
{
	clientid->cid_lease_reservations--;

	/* Renew lease when last reservation is released */
	if (clientid->cid_lease_reservations == 0)
		clientid->cid_last_renew = time(NULL);

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_client_id_rec(&dspbuf, clientid);
		LogFullDebug(COMPONENT_CLIENTID, "Update Lease %s", str);
	}
}

/** @} */
