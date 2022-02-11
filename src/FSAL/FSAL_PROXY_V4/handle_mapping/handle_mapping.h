/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 */

/*
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-130  USA
 *
 * ---------------------------------------
 */

/**
 * @file   handle_mapping.h
 *
 * @brief  This module is used for managing a persistent map
 *         between PROXY_V4 FSAL handles (including NFSv4 handles from server)
 *         and nfsv3 handles digests (sent to client).
 */
#ifndef _HANDLE_MAPPING_H
#define _HANDLE_MAPPING_H

#include "fsal.h"

/* parameters for Handle Map module */
typedef struct handle_map_param__ {
	/* path where database files are located */
	char *databases_directory;

	/* temp dir for database work */
	char *temp_directory;

	/* number of databases */
	unsigned int database_count;

	/* hash table size */
	unsigned int hashtable_size;

	/* synchronous insert mode */
	int synchronous_insert;

} handle_map_param_t;

/* this describes a handle digest for nfsv3 */

#define PROXYV4_HANDLE_MAPPED 0x23

typedef struct nfs23_map_handle__ {
	uint8_t len;
	uint8_t type;		/* Must be PROXYV4_HANDLE_MAPPED */
	/* to avoid reusing handles, when object_id is reused */
	unsigned int handle_hash;
	/* object id */
	uint64_t object_id;

} nfs23_map_handle_t;

/* Error codes */
#define HANDLEMAP_SUCCESS        0
#define HANDLEMAP_STALE          1
#define HANDLEMAP_INCONSISTENCY  2
#define HANDLEMAP_DB_ERROR       3
#define HANDLEMAP_SYSTEM_ERROR   4
#define HANDLEMAP_INTERNAL_ERROR 5
#define HANDLEMAP_INVALID_PARAM  6
#define HANDLEMAP_HASHTABLE_ERROR 7
#define HANDLEMAP_EXISTS         8

int HandleMap_Init(const handle_map_param_t *p_param);

int HandleMap_GetFH(const nfs23_map_handle_t *, struct gsh_buffdesc *);

int HandleMap_SetFH(nfs23_map_handle_t *p_in_nfs23_digest,
		    const void *p_in_handle, uint32_t len);

int HandleMap_DelFH(nfs23_map_handle_t *p_in_nfs23_digest);

int HandleMap_Flush(void);

#endif
