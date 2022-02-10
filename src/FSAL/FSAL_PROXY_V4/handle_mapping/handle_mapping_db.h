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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

#ifndef _HANDLE_MAPPING_DB_H
#define _HANDLE_MAPPING_DB_H

#include "handle_mapping.h"
#include "hashtable.h"

#define DB_FILE_PREFIX "handlemap.sqlite"

/* Database definition */
#define MAP_TABLE      "HandleMap"
#define OBJID_FIELD    "ObjectId"
#define HASH_FIELD     "HandleHash"
#define HANDLE_FIELD   "FSALHandle"

#define MAX_DB  32

/**
 * count the number of database instances in a given directory
 * (this is used for checking that the number of db
 * matches the number of threads)
 */
int handlemap_db_count(const char *dir);

/**
 * Initialize databases access
 * (init DB queues, start threads, establish DB connections,
 * and create db schema if it was empty).
 */
int handlemap_db_init(const char *db_dir, const char *tmp_dir,
		      unsigned int db_count, int synchronous_insert);

/**
 * Gives the order to each DB thread to reload
 * the content of its database and insert it
 * to the hash table.
 * The function blocks until all threads have loaded their data.
 */
int handlemap_db_reaload_all(hash_table_t *target_hash);

/**
 * Submit a db 'insert' request.
 * The request is inserted in the appropriate db queue.
 */
int handlemap_db_insert(nfs23_map_handle_t *p_in_nfs23_digest,
			const void *data, uint32_t len);

/**
 * Submit a db 'delete' request.
 * The request is inserted in the appropriate db queue.
 * (always asynchronous)
 */
int handlemap_db_delete(nfs23_map_handle_t *p_in_nfs23_digest);

/**
 * Wait for all queues to be empty
 * and all current DB request to be done.
 */
int handlemap_db_flush(void);

#endif
