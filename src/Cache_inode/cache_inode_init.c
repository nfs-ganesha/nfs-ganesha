/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
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
 * Copyright CEA/DAM/DIF (2005)
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
 * \file    cache_inode_init.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:44:40 $
 * \version $Revision: 1.21 $
 * \brief   Init the cache_inode.
 *
 * cache_inode_init.c : Initialization routines for the cache_inode.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif /* _SOLARIS */

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>


/**
 *
 * cache_inode_init: Init the ressource necessary for the cache inode management.
 * 
 * Init the ressource necessary for the cache inode management.
 * 
 * @param param [IN] the parameter for this cache. 
 * @param pstatus [OUT] pointer to buffer used to store the status for the operation.
 *
 * @return NULL if operation failed, other value is a pointer to the hash table used for the cache.
 *
 */
hash_table_t * cache_inode_init( cache_inode_parameter_t param, cache_inode_status_t *pstatus )
{
  hash_table_t * ht = NULL ;

  ht = HashTable_Init( param.hparam ) ;

  if( ht != NULL )
    *pstatus = CACHE_INODE_SUCCESS ;
  else
    *pstatus = CACHE_INODE_INVALID_ARGUMENT ;

  DisplayLog( "Hash Table initiated" ) ;
 
  return ht ;
} /* cache_inode_init */


/**
 *
 * cache_inode_client_init: Init the ressource necessary for the cache inode management on the client handside.
 * 
 * Init the ressource necessary for the cache inode management on the client handside.
 * 
 * @param pclient      [OUT] the pointer to the client to be initiated.
 * @param param        [IN]  the parameter for this cache client. 
 * @param thread_index [IN]  an integer related to the 'position' of the thread, from 0 to Nb_Workers -1 
 *
 * @return 0 if successful, 1 if failed. 
 *
 */
int cache_inode_client_init( cache_inode_client_t * pclient, 
                             cache_inode_client_parameter_t param, 
                             int thread_index, 
			     void * pworker_data )
{
  LRU_status_t lru_status ;

  pclient->log_outputs              = param.log_outputs ;
  pclient->attrmask                 = param.attrmask ;
  pclient->nb_prealloc              = param.nb_prealloc_entry ;
  pclient->nb_pre_dir_data          = param.nb_pre_dir_data ;
  pclient->nb_pre_parent            = param.nb_pre_parent ;
  pclient->nb_pre_state_v4          = param.nb_pre_state_v4 ;
  pclient->grace_period_attr        = param.grace_period_attr ;
  pclient->grace_period_link        = param.grace_period_link ;
  pclient->grace_period_dirent      = param.grace_period_dirent ;
  pclient->use_test_access          = param.use_test_access ;
  pclient->getattr_dir_invalidation = param.getattr_dir_invalidation ; 
  pclient->pworker                  = pworker_data ;
  pclient->use_cache                = param.use_cache ;
  pclient->retention                = param.retention ;
  pclient->max_fd_per_thread        = param.max_fd_per_thread ;

  /* introducing desynchronisation for GC */
  pclient->time_of_last_gc          = time( NULL ) + thread_index * 20 ;
  pclient->call_since_last_gc       = thread_index * 20;

  pclient->time_of_last_gc_fd       = time( NULL );

    
#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel( "cache_entry_t" ) ;
#endif

#ifndef _NO_BLOCK_PREALLOC
  STUFF_PREALLOC( pclient->pool_entry, 
                  pclient->nb_prealloc, 
                  cache_entry_t, 
                  next_alloc ) ;
  if( pclient->pool_entry == NULL )
    {
      DisplayLogJd( pclient->log_outputs, "Error : can't init cache_inode client entry pool" ) ;
      return 1 ;
    }
#endif
  
#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel( "cache_inode_dir_data_t" ) ;
#endif

#ifndef _NO_BLOCK_PREALLOC
  STUFF_PREALLOC( pclient->pool_dir_data, 
                  pclient->nb_pre_dir_data, 
                  cache_inode_dir_data_t, 
                  next_alloc ) ;
  if( pclient->pool_dir_data == NULL )
    { 
      DisplayLogJd( pclient->log_outputs, "Error : can't init cache_inode client dir data pool" ) ;
      return 1 ;
    }
#endif
      
#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel( "cache_inode_parent_entry_t" ) ;
#endif

#ifndef _NO_BLOCK_PREALLOC
  STUFF_PREALLOC( pclient->pool_parent,
                  pclient->nb_pre_parent,
                  cache_inode_parent_entry_t, 
                  next_alloc ) ;
  if( pclient->pool_parent == NULL )
    { 
      DisplayLogJd( pclient->log_outputs, "Error : can't init cache_inode client parent link pool" ) ;
      return 1 ;
    }
#endif
 
#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel( "cache_inode_state_t" ) ;
#endif
 
#ifndef _NO_BLOCK_PREALLOC
  STUFF_PREALLOC( pclient->pool_state_v4,
                  pclient->nb_pre_state_v4,
                  cache_inode_state_t,
                  next ) ;
  if( pclient->pool_state_v4 == NULL )
    {
      DisplayLogJd( pclient->log_outputs, "Error : can't init cache_inode client state v4 pool" ) ;
      return 1 ;
    }
#endif

  
#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel( "cache_inode_fsal_data_t:init" ) ;
#endif

#ifndef _NO_BLOCK_PREALLOC
  STUFF_PREALLOC( pclient->pool_key, 
                  pclient->nb_prealloc, 
                  cache_inode_fsal_data_t, 
                  next_alloc ) ;
  
# ifdef _DEBUG_MEMLEAKS
    /* For debugging memory leaks */
    BuddySetDebugLabel( "N/A" ) ;
# endif

  if( pclient->pool_key == NULL )
    {
      DisplayLogJd( pclient->log_outputs, "Error : can't init cache_inode client key pool" ) ;
      return 1 ;
    }
#endif  

  if( ( pclient->lru_gc = LRU_Init( param.lru_param, &lru_status ) ) == NULL )
    {
      DisplayLogJd( pclient->log_outputs, "Error : can't init cache_inode client lru gc" ) ;
      return 1  ;
    }

  /* Everything was ok, return 0 */
  return 0 ;
} /* cache_inode_client_init */
