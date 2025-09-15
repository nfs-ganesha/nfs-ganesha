// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2025, IBM . All rights reserved.
 * Author: Naresh Chillarege<Naresh.Chillarege@ibm.com>
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
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/
 *
 * ---------------------------------------
 */

#include "nfs_core.h"
#include "cqos.h"
#include "gsh_rpc.h"

bool xdr_cqos_cmd_type_t (XDR *xdrs, cqos_cmd_type_t *objp)
{
	if (!xdr_enum(xdrs, (enum_t *)objp))
		return false;
	return true;
}

bool xdr_sockaddr_t(XDR *xdrs, sockaddr_t *objp)
{
	if (!xdr_opaque(xdrs, (char *)objp, sizeof(sockaddr_t)))
		return false;
	return true;
}

bool xdr_cluster_qos_msg (XDR *xdrs, cluster_qos_msg *objp)
{
	if (!xdr_cqos_cmd_type_t (xdrs, &objp->cqos_cmd))
		return false;
	if (!xdr_int32_t (xdrs, &objp->cqos_ops))
		return false;
	if (!xdr_uint16_t (xdrs, &objp->export_id))
		return false;
	if (!xdr_uint64_t (xdrs, &objp->export_rbw))
		return false;
	if (!xdr_uint64_t (xdrs, &objp->export_wbw))
		return false;
	if (!xdr_uint64_t (xdrs, &objp->export_riops))
		return false;
	if (!xdr_uint64_t (xdrs, &objp->export_wiops))
		return false;
	if (!xdr_uint64_t (xdrs, &objp->client_rbw))
		return false;
	if (!xdr_uint64_t (xdrs, &objp->client_wbw))
		return false;
	if (!xdr_uint64_t (xdrs, &objp->client_riops))
		return false;
	if (!xdr_uint64_t (xdrs, &objp->client_wiops))
		return false;
	if (!xdr_sockaddr_t (xdrs, &objp->node_addr))
		return false;
	if (!xdr_sockaddr_t (xdrs, &objp->client_addr))
		return false;
	return true;
}
