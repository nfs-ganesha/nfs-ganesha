/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file   handle_mapping.h
 *
 * \brief  This module is used for managing a persistent
 *         map between PROXY FSAL handles (including NFSv4 handles from server)
 *         and nfsv2 and v3 handles digests (sent to client).
 */
#ifndef _HANDLE_MAPPING_H
#define _HANDLE_MAPPING_H

#include "fsal.h"

/* parameters for Handle Map module */
typedef struct handle_map_param__
{
  /* path where database files are located */
  char databases_directory[MAXPATHLEN];

  /* temp dir for database work */
  char temp_directory[MAXPATHLEN];

  /* number of databases */
  unsigned int database_count;

  /* hash table size */
  unsigned int hashtable_size;

  /* number of preallocated FSAL handles */
  unsigned int nb_handles_prealloc;

  /* number of preallocated DB operations */
  unsigned int nb_db_op_prealloc;

  /* synchronous insert mode */
  int synchronous_insert;

} handle_map_param_t;

/* this describes a handle digest for nfsv2 and nfsv3 */

typedef struct nfs23_map_handle__
{
  /* object id */
  uint64_t object_id;

  /* to avoid reusing handles, when object_id is reused */
  unsigned int handle_hash;

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

/**
 * Init handle mapping module.
 * Reloads the content of the mapping files it they exist,
 * else it creates them.
 * \return 0 if OK, an error code else.
 */
int HandleMap_Init(const handle_map_param_t * p_param);

/**
 * Retrieves a full fsal_handle from a NFS2/3 digest.
 *
 * \param  p_nfs23_digest   [in] the NFS2/3 handle digest
 * \param  p_out_fsal_handle [out] the fsal handle to be retrieved
 *
 * \return HANDLEMAP_SUCCESS if the handle is available,
 *         HANDLEMAP_STALE if the disgest is unknown or the handle has been deleted
 */
int HandleMap_GetFH(nfs23_map_handle_t * p_in_nfs23_digest,
                    fsal_handle_t * p_out_fsal_handle);

/**
 * Save the handle association if it was unknown.
 */
int HandleMap_SetFH(nfs23_map_handle_t * p_in_nfs23_digest, fsal_handle_t * p_in_handle);

/**
 * Remove a handle from the map
 * when it was removed from the filesystem
 * or when it is stale.
 */
int HandleMap_DelFH(nfs23_map_handle_t * p_in_nfs23_digest);

/**
 * Flush pending database operations (before stopping the server).
 */
int HandleMap_Flush();

#endif
