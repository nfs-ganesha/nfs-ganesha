/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/*
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
 * Copyright CEA/DAM/DIF (2008)
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
typedef struct handle_map_param__ {
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

typedef struct nfs23_map_handle__ {
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
