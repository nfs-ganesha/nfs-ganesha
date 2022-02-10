/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * @file common_utils.h
 * @brief Common tools for printing, parsing, ....
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

#ifndef _NFS_IP_STATS_H
#define _NFS_IP_STATS_H

#include <sys/types.h>
#include <sys/param.h>

#include "gsh_rpc.h"
#include <netdb.h>		/* for having MAXHOSTNAMELEN */
#include "hashtable.h"

/* IP/name cache error */
#define IP_NAME_SUCCESS             0
#define IP_NAME_INSERT_MALLOC_ERROR 1
#define IP_NAME_NOT_FOUND           2

#define IP_NAME_PREALLOC_SIZE      200

/* NFS IPaddr cache entry structure */
typedef struct nfs_ip_name__ {
	time_t timestamp;
	char hostname[];
} nfs_ip_name_t;

int nfs_ip_name_get(sockaddr_t *ipaddr, char *hostname, size_t size);
int nfs_ip_name_add(sockaddr_t *ipaddr, char *hostname, size_t size);
int nfs_ip_name_remove(sockaddr_t *ipaddr);

#endif
